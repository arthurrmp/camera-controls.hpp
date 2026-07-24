#import "Viewer.h"

#include "camera_controls.hpp"

#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/View.h>
#include <filament/Viewport.h>

#include <image/Ktx1Bundle.h>
#include <ktxreader/Ktx1Reader.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <gltfio/materials/uberarchive.h>

#include <math/mat4.h>
#include <math/vec3.h>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

#include <cmath>

using namespace filament;
using namespace filament::gltfio;
using namespace utils;

static constexpr double kFovDegrees = 45.0;

@implementation Viewer {
    Engine *_engine;
    SwapChain *_swapChain;
    Renderer *_renderer;
    Scene *_scene;
    View *_view;
    Camera *_camera;
    Entity _cameraEntity;
    Entity _sun;
    MaterialProvider *_materials;
    AssetLoader *_assetLoader;
    ResourceLoader *_resourceLoader;
    TextureProvider *_stbProvider;
    FilamentAsset *_asset;
    IndirectLight *_ibl;
    Texture *_iblTexture;
    camctl::CameraControls _controls;
    double _tanHalfFov;
    uint32_t _width;
    uint32_t _height;
    math::float3 _modelCenter;
    double _modelRadius;
    BOOL _needsFraming;
}

- (instancetype)initWithLayer:(CAMetalLayer *)layer {
    self = [super init];
    if (!self) return nil;

    _engine = Engine::create(Engine::Backend::METAL);
    _swapChain = _engine->createSwapChain((__bridge void *)layer);
    _renderer = _engine->createRenderer();
    _scene = _engine->createScene();
    _view = _engine->createView();

    _cameraEntity = EntityManager::get().create();
    _camera = _engine->createCamera(_cameraEntity);
    _view->setScene(_scene);
    _view->setCamera(_camera);

    View::MultiSampleAntiAliasingOptions msaa;
    msaa.enabled = true;
    _view->setMultiSampleAntiAliasingOptions(msaa);

    Renderer::ClearOptions clearOptions;
    clearOptions.clear = true;
    clearOptions.clearColor = {0.05f, 0.05f, 0.07f, 1.0f};
    _renderer->setClearOptions(clearOptions);

    // Key light. The image-based light from loadEnvironment: fills the rest.
    _sun = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color({1.0f, 0.98f, 0.94f})
        .intensity(70000.0f)
        .direction(normalize(math::float3{-0.5f, -1.0f, -0.6f}))
        .castShadows(false)
        .build(*_engine, _sun);
    _scene->addEntity(_sun);

    // gltfio with the ubershader materials: no material compilation at
    // build time.
    _materials = createUbershaderProvider(
        _engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);
    AssetConfiguration assetConfig{};
    assetConfig.engine = _engine;
    assetConfig.materials = _materials;
    _assetLoader = AssetLoader::create(assetConfig);
    ResourceConfiguration resourceConfig{};
    resourceConfig.engine = _engine;
    resourceConfig.gltfPath = "";
    _resourceLoader = new ResourceLoader(resourceConfig);
    _stbProvider = createStbProvider(_engine);
    _resourceLoader->addTextureProvider("image/png", _stbProvider);
    _resourceLoader->addTextureProvider("image/jpeg", _stbProvider);

    _tanHalfFov = std::tan(kFovDegrees * 0.5 * M_PI / 180.0);
    _modelRadius = 1.0;
    return self;
}

/// Image-based light from a KTX1 cubemap with spherical harmonics metadata
/// (a cmgen output, for example Filament's default_env).
- (BOOL)loadEnvironment:(NSData *)ktx {
    auto *bundle = new image::Ktx1Bundle((const uint8_t *)ktx.bytes,
                                         (uint32_t)ktx.length);
    math::float3 harmonics[9];
    bundle->getSphericalHarmonics(harmonics);
    _iblTexture = ktxreader::Ktx1Reader::createTexture(_engine, bundle, false);
    if (!_iblTexture) return NO;
    _ibl = IndirectLight::Builder()
        .reflections(_iblTexture)
        .irradiance(3, harmonics)
        .intensity(30000.0f)
        .build(*_engine);
    _scene->setIndirectLight(_ibl);
    return YES;
}

- (BOOL)loadModel:(NSData *)glb {
    _asset = _assetLoader->createAsset((const uint8_t *)glb.bytes,
                                       (uint32_t)glb.length);
    if (!_asset) return NO;
    if (!_resourceLoader->loadResources(_asset)) return NO;
    _asset->releaseSourceData();
    _scene->addEntities(_asset->getEntities(), _asset->getEntityCount());

    const Aabb box = _asset->getBoundingBox();
    const math::float3 extent = box.extent(); // half extent
    _modelCenter = box.center();
    _modelRadius = std::sqrt(extent.x * extent.x +
                             extent.y * extent.y +
                             extent.z * extent.z);
    // The frame distance depends on the viewport aspect, which the first
    // resize supplies.
    _needsFraming = YES;
    if (_width > 0) [self frameModel];
    return YES;
}

/// Frame the model with fitToSphere, then start 20 degrees above the
/// horizon.
- (void)frameModel {
    _needsFraming = NO;
    [self updateProjection];
    const double aspect = (double)_width / (double)_height;
    _controls.minDistance = _modelRadius * 0.5;
    _controls.maxDistance = _modelRadius * 20.0;
    _controls.fitToSphere({_modelCenter.x, _modelCenter.y, _modelCenter.z},
                          _modelRadius, false,
                          kFovDegrees * M_PI / 180.0, aspect);
    _controls.rotateTo(0.0, 70.0 * M_PI / 180.0, false);
}

- (void)resizeWidth:(uint32_t)width height:(uint32_t)height {
    if (width == 0 || height == 0) return;
    _width = width;
    _height = height;
    _view->setViewport({0, 0, width, height});
    [self updateProjection];
    if (_needsFraming) [self frameModel];
}

/// The near and far planes scale with the model, so small models (the
/// Avocado is 6cm) do not clip.
- (void)updateProjection {
    const double aspect = (double)_width / (double)_height;
    const double nearPlane = std::max(1e-4, _modelRadius * 0.02);
    const double farPlane = std::max(100.0, _modelRadius * 1000.0);
    _camera->setProjection(kFovDegrees, aspect, nearPlane, farPlane,
                           Camera::Fov::VERTICAL);
}

- (void)render:(double)dt {
    _controls.update(dt);
    const camctl::Vec3 eye = _controls.getPosition(false);
    const camctl::Vec3 target = _controls.getTarget(false);
    // Not camera->lookAt(): see the doc comment on CameraControls::lookAt.
    const auto b = camctl::CameraControls::lookAt(eye, target);
    _camera->setModelMatrix(math::mat4{
        math::double4{b.x.x, b.x.y, b.x.z, 0.0},
        math::double4{b.y.x, b.y.y, b.y.z, 0.0},
        math::double4{b.z.x, b.z.y, b.z.z, 0.0},
        math::double4{eye.x, eye.y, eye.z, 1.0}});

    if (_renderer->beginFrame(_swapChain)) {
        _renderer->render(_view);
        _renderer->endFrame();
    }
}

- (void)rotateDx:(double)dx dy:(double)dy {
    _controls.rotatePixels(dx, dy, _height);
}

- (void)pinchDollyDelta:(double)deltaPoints {
    _controls.dollyPinchDelta(deltaPoints);
}

- (void)pinchTruckDx:(double)dx dy:(double)dy {
    _controls.truckPixels(dx, dy, _height, _tanHalfFov);
}

- (void)anchoredDollyDelta:(double)deltaPoints anchorX:(double)x anchorY:(double)y {
    _controls.dollyDeltaAnchored(deltaPoints, x, y, _width, _height, _tanHalfFov);
}

- (void)endRotate {
    _controls.endRotate();
}

- (void)endPinch {
    _controls.endDolly();
    _controls.endTruck();
}

- (void)shutdown {
    if (!_engine) return;
    if (_asset) {
        _scene->removeEntities(_asset->getEntities(), _asset->getEntityCount());
        _assetLoader->destroyAsset(_asset);
        _asset = nullptr;
    }
    if (_ibl) _engine->destroy(_ibl);
    if (_iblTexture) _engine->destroy(_iblTexture);
    delete _resourceLoader;
    delete _stbProvider;
    AssetLoader::destroy(&_assetLoader);
    _materials->destroyMaterials();
    delete _materials;
    _scene->remove(_sun);
    _engine->destroy(_sun);
    _engine->destroyCameraComponent(_cameraEntity);
    _engine->destroy(_cameraEntity);
    _engine->destroy(_view);
    _engine->destroy(_scene);
    _engine->destroy(_renderer);
    _engine->destroy(_swapChain);
    Engine::destroy(&_engine);
    _engine = nullptr;
}

@end
