// Vulkan example for camera-controls.hpp: a lit cube with GLFW, no engine.
// The camera comes from the library's mouse layer; the view matrix is built
// from the lookAt basis. On macOS this runs through MoltenVK.
//
// The SPIR-V is embedded (shader_vert.h / shader_frag.h). To regenerate:
//   glslc shader.vert -o shader.vert.spv && xxd -i shader.vert.spv > shader_vert.h
//   glslc shader.frag -o shader.frag.spv && xxd -i shader.frag.spv > shader_frag.h

#include "camera_controls.hpp"
#include "shader_frag.h"
#include "shader_vert.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static constexpr double kFovDegrees = 45.0;
static constexpr int kFramesInFlight = 2;

#define VK_CHECK(call)                                                        \
    do {                                                                      \
        const VkResult r = (call);                                            \
        if (r != VK_SUCCESS) {                                                \
            std::fprintf(stderr, "%s failed: %d\n", #call, (int)r);           \
            std::abort();                                                     \
        }                                                                     \
    } while (0)

// ---- Small column-major mat4, enough for one example --------------------

struct Mat4 {
    float m[16];
};

static Mat4 mul(const Mat4 &a, const Mat4 &b) {
    Mat4 r{};
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

/// The basis vectors are the columns of the camera model matrix, so the
/// view matrix (its inverse) has them as rows, with -dot(axis, eye) as
/// the translation.
static Mat4 viewMatrix(const camctl::CameraControls::Basis &b, const camctl::Vec3 &eye) {
    return {{(float)b.x.x, (float)b.y.x, (float)b.z.x, 0,
             (float)b.x.y, (float)b.y.y, (float)b.z.y, 0,
             (float)b.x.z, (float)b.y.z, (float)b.z.z, 0,
             (float)-dot(b.x, eye), (float)-dot(b.y, eye), (float)-dot(b.z, eye), 1}};
}

/// Right-handed, depth 0..1, Y down (the Vulkan clip space).
static Mat4 projectionMatrix(double fovYRadians, double aspect,
                             double nearPlane, double farPlane) {
    const float ys = 1.0f / (float)std::tan(fovYRadians * 0.5);
    const float xs = ys / (float)aspect;
    const float zs = (float)(farPlane / (nearPlane - farPlane));
    Mat4 r{};
    r.m[0] = xs;
    r.m[5] = -ys;
    r.m[10] = zs;
    r.m[11] = -1.0f;
    r.m[14] = (float)nearPlane * zs;
    return r;
}

// ---- Geometry ------------------------------------------------------------

struct Vertex {
    float position[3];
    float normal[3];
};

struct PushConstants {
    Mat4 viewProjection;
    float lightDirection[4];
};

static void buildCube(std::vector<Vertex> &vertices, std::vector<uint16_t> &indices) {
    static const float normals[6][3] = {
        {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
    };
    for (int face = 0; face < 6; face++) {
        const float *n = normals[face];
        const float up[3] = {0, std::abs(n[1]) > 0.5f ? 0.0f : 1.0f,
                             std::abs(n[1]) > 0.5f ? 1.0f : 0.0f};
        const float right[3] = {up[1] * n[2] - up[2] * n[1],
                                up[2] * n[0] - up[0] * n[2],
                                up[0] * n[1] - up[1] * n[0]};
        const float realUp[3] = {n[1] * right[2] - n[2] * right[1],
                                 n[2] * right[0] - n[0] * right[2],
                                 n[0] * right[1] - n[1] * right[0]};
        const uint16_t base = (uint16_t)vertices.size();
        for (int corner = 0; corner < 4; corner++) {
            const float sr = (corner == 1 || corner == 2) ? 1.0f : -1.0f;
            const float su = (corner >= 2) ? 1.0f : -1.0f;
            vertices.push_back({{n[0] + sr * right[0] + su * realUp[0],
                                 n[1] + sr * right[1] + su * realUp[1],
                                 n[2] + sr * right[2] + su * realUp[2]},
                                {n[0], n[1], n[2]}});
        }
        const uint16_t quad[6] = {base, (uint16_t)(base + 1), (uint16_t)(base + 2),
                                  base, (uint16_t)(base + 2), (uint16_t)(base + 3)};
        indices.insert(indices.end(), quad, quad + 6);
    }
}

// ---- Application ----------------------------------------------------------

struct App {
    GLFWwindow *window = nullptr;
    camctl::CameraControls controls;
    bool framed = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffers[kFramesInFlight]{};
    VkSemaphore imageAvailable[kFramesInFlight]{};
    VkSemaphore renderFinished[kFramesInFlight]{};
    VkFence inFlight[kFramesInFlight]{};
    int frame = 0;
};

static uint32_t findMemoryType(App &app, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(app.physical, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    std::fprintf(stderr, "no suitable memory type\n");
    std::abort();
}

static void createHostBuffer(App &app, VkBufferUsageFlags usage, const void *data,
                             VkDeviceSize size, VkBuffer &buffer, VkDeviceMemory &memory) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(app.device, &bufferInfo, nullptr, &buffer));

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(app.device, buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(app, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(app.device, &alloc, nullptr, &memory));
    VK_CHECK(vkBindBufferMemory(app.device, buffer, memory, 0));

    void *mapped = nullptr;
    VK_CHECK(vkMapMemory(app.device, memory, 0, size, 0, &mapped));
    std::memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(app.device, memory);
}

static VkShaderModule createShaderModule(App &app, const unsigned char *code, unsigned len) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = len;
    info.pCode = (const uint32_t *)code;
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(app.device, &info, nullptr, &module));
    return module;
}

static void destroySwapchain(App &app) {
    for (VkFramebuffer fb : app.framebuffers) vkDestroyFramebuffer(app.device, fb, nullptr);
    app.framebuffers.clear();
    for (VkImageView view : app.imageViews) vkDestroyImageView(app.device, view, nullptr);
    app.imageViews.clear();
    if (app.depthView) vkDestroyImageView(app.device, app.depthView, nullptr);
    if (app.depthImage) vkDestroyImage(app.device, app.depthImage, nullptr);
    if (app.depthMemory) vkFreeMemory(app.device, app.depthMemory, nullptr);
    app.depthView = VK_NULL_HANDLE;
    app.depthImage = VK_NULL_HANDLE;
    app.depthMemory = VK_NULL_HANDLE;
    if (app.swapchain) vkDestroySwapchainKHR(app.device, app.swapchain, nullptr);
    app.swapchain = VK_NULL_HANDLE;
}

static void createSwapchain(App &app) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(app.window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(app.window, &width, &height);
    }
    vkDeviceWaitIdle(app.device);
    destroySwapchain(app);

    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app.physical, app.surface, &caps));
    app.extent = caps.currentExtent;
    if (app.extent.width == 0xFFFFFFFF) {
        app.extent = {(uint32_t)width, (uint32_t)height};
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app.physical, app.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(app.physical, app.surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto &f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) chosen = f;
    }
    app.colorFormat = chosen.format;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = app.surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = app.extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(app.device, &info, nullptr, &app.swapchain));

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(app.device, app.swapchain, &count, nullptr);
    app.images.resize(count);
    vkGetSwapchainImagesKHR(app.device, app.swapchain, &count, app.images.data());

    app.imageViews.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = app.images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = app.colorFormat;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(app.device, &view, nullptr, &app.imageViews[i]));
    }

    // Depth attachment.
    VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = VK_FORMAT_D32_SFLOAT;
    depthInfo.extent = {app.extent.width, app.extent.height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VK_CHECK(vkCreateImage(app.device, &depthInfo, nullptr, &app.depthImage));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(app.device, app.depthImage, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(app, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(app.device, &alloc, nullptr, &app.depthMemory));
    VK_CHECK(vkBindImageMemory(app.device, app.depthImage, app.depthMemory, 0));
    VkImageViewCreateInfo depthViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    depthViewInfo.image = app.depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
    depthViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(app.device, &depthViewInfo, nullptr, &app.depthView));

    app.framebuffers.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        const VkImageView attachments[2] = {app.imageViews[i], app.depthView};
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = app.renderPass;
        fb.attachmentCount = 2;
        fb.pAttachments = attachments;
        fb.width = app.extent.width;
        fb.height = app.extent.height;
        fb.layers = 1;
        VK_CHECK(vkCreateFramebuffer(app.device, &fb, nullptr, &app.framebuffers[i]));
    }

    // The touch and mouse layers work in window coordinates (points).
    int winW = 0, winH = 0;
    glfwGetWindowSize(app.window, &winW, &winH);
    app.controls.setViewport(winW, winH, std::tan(kFovDegrees * 0.5 * M_PI / 180.0));
    if (!app.framed) {
        app.framed = true;
        app.controls.minDistance = 1.0;
        app.controls.maxDistance = 60.0;
        app.controls.fitToSphere({0, 0, 0}, std::sqrt(3.0), false,
                                 kFovDegrees * M_PI / 180.0,
                                 (double)app.extent.width / (double)app.extent.height);
        app.controls.rotateTo(0.0, 70.0 * M_PI / 180.0, false);
    }
}

int main() {
    App app;

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    app.window = glfwCreateWindow(960, 720, "camera-controls.hpp - Vulkan", nullptr, nullptr);
    if (!app.window) return 1;
    glfwSetWindowUserPointer(app.window, &app);

    // Instance. The portability extension makes MoltenVK visible.
    uint32_t glfwExtCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char *> instanceExts(glfwExts, glfwExts + glfwExtCount);
    VkInstanceCreateInfo instInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.apiVersion = VK_API_VERSION_1_1;
    instInfo.pApplicationInfo = &appInfo;
#ifdef VK_KHR_portability_enumeration
    instanceExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instanceExts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    instInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    instInfo.enabledExtensionCount = (uint32_t)instanceExts.size();
    instInfo.ppEnabledExtensionNames = instanceExts.data();
    VK_CHECK(vkCreateInstance(&instInfo, nullptr, &app.instance));

    VK_CHECK(glfwCreateWindowSurface(app.instance, app.window, nullptr, &app.surface));

    // Physical device: the first one with a graphics queue that can present.
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(app.instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(app.instance, &deviceCount, devices.data());
    for (VkPhysicalDevice candidate : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, app.surface, &present);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                app.physical = candidate;
                app.queueFamily = i;
                break;
            }
        }
        if (app.physical) break;
    }
    if (!app.physical) {
        std::fprintf(stderr, "no suitable Vulkan device\n");
        return 1;
    }

    // Device. portability_subset must be enabled when the driver offers it.
    std::vector<const char *> deviceExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(app.physical, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(app.physical, nullptr, &extCount, exts.data());
    for (const auto &e : exts) {
        if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) {
            deviceExts.push_back("VK_KHR_portability_subset");
        }
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = app.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = (uint32_t)deviceExts.size();
    deviceInfo.ppEnabledExtensionNames = deviceExts.data();
    VK_CHECK(vkCreateDevice(app.physical, &deviceInfo, nullptr, &app.device));
    vkGetDeviceQueue(app.device, app.queueFamily, 0, &app.queue);

    // Render pass: one color attachment, one depth attachment.
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    VK_CHECK(vkCreateRenderPass(app.device, &rpInfo, nullptr, &app.renderPass));

    // Pipeline: push constants only, no descriptor sets.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(app.device, &layoutInfo, nullptr, &app.pipelineLayout));

    VkShaderModule vert = createShaderModule(app, shader_vert_spv, shader_vert_spv_len);
    VkShaderModule frag = createShaderModule(app, shader_frag_spv, shader_frag_spv_len);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = app.pipelineLayout;
    pipelineInfo.renderPass = app.renderPass;
    VK_CHECK(vkCreateGraphicsPipelines(app.device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                       nullptr, &app.pipeline));
    vkDestroyShaderModule(app.device, vert, nullptr);
    vkDestroyShaderModule(app.device, frag, nullptr);

    // Geometry.
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    buildCube(vertices, indices);
    app.indexCount = (uint32_t)indices.size();
    createHostBuffer(app, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data(),
                     vertices.size() * sizeof(Vertex), app.vertexBuffer, app.vertexMemory);
    createHostBuffer(app, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data(),
                     indices.size() * sizeof(uint16_t), app.indexBuffer, app.indexMemory);

    // Commands and synchronization.
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = app.queueFamily;
    VK_CHECK(vkCreateCommandPool(app.device, &poolInfo, nullptr, &app.commandPool));
    VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbInfo.commandPool = app.commandPool;
    cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = kFramesInFlight;
    VK_CHECK(vkAllocateCommandBuffers(app.device, &cbInfo, app.commandBuffers));
    for (int i = 0; i < kFramesInFlight; i++) {
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(app.device, &semInfo, nullptr, &app.imageAvailable[i]));
        VK_CHECK(vkCreateSemaphore(app.device, &semInfo, nullptr, &app.renderFinished[i]));
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(app.device, &fenceInfo, nullptr, &app.inFlight[i]));
    }

    createSwapchain(app);

    // Mouse: GLFW numbers buttons left 0, right 1, middle 2; the library
    // numbers them left 0, middle 1, right 2.
    glfwSetMouseButtonCallback(app.window, [](GLFWwindow *w, int button, int action, int) {
        App &a = *(App *)glfwGetWindowUserPointer(w);
        if (action == GLFW_PRESS) {
            double x, y;
            glfwGetCursorPos(w, &x, &y);
            const int mapped = button == GLFW_MOUSE_BUTTON_RIGHT   ? 2
                               : button == GLFW_MOUSE_BUTTON_MIDDLE ? 1
                                                                    : 0;
            a.controls.mouseDown(mapped, x, y);
        } else if (action == GLFW_RELEASE) {
            a.controls.mouseUp();
        }
    });
    glfwSetCursorPosCallback(app.window, [](GLFWwindow *w, double x, double y) {
        App &a = *(App *)glfwGetWindowUserPointer(w);
        a.controls.mouseMoved(x, y);
    });
    // A GLFW wheel tick is about one line; 30 approximates the web's
    // pixel-mode delta for one tick on non-mac systems.
    glfwSetScrollCallback(app.window, [](GLFWwindow *w, double, double yoffset) {
        App &a = *(App *)glfwGetWindowUserPointer(w);
        double x, y;
        glfwGetCursorPos(w, &x, &y);
        a.controls.mouseWheel(-yoffset * 30.0, x, y);
    });

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(app.window)) {
        glfwPollEvents();
        const double now = glfwGetTime();
        const double dt = now - lastTime;
        lastTime = now;

        app.controls.update(dt);
        const camctl::Vec3 eye = app.controls.getPosition(false);
        const camctl::Vec3 target = app.controls.getTarget(false);
        const auto basis = camctl::CameraControls::lookAt(eye, target);

        PushConstants push{};
        const Mat4 view = viewMatrix(basis, eye);
        const Mat4 projection = projectionMatrix(
            kFovDegrees * M_PI / 180.0,
            (double)app.extent.width / (double)app.extent.height, 0.1, 200.0);
        push.viewProjection = mul(projection, view);
        const float lightLen = std::sqrt(0.5f * 0.5f + 1.0f + 0.6f * 0.6f);
        push.lightDirection[0] = -0.5f / lightLen;
        push.lightDirection[1] = -1.0f / lightLen;
        push.lightDirection[2] = -0.6f / lightLen;

        const int f = app.frame;
        vkWaitForFences(app.device, 1, &app.inFlight[f], VK_TRUE, UINT64_MAX);
        uint32_t imageIndex = 0;
        VkResult acquire = vkAcquireNextImageKHR(app.device, app.swapchain, UINT64_MAX,
                                                 app.imageAvailable[f], VK_NULL_HANDLE,
                                                 &imageIndex);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            createSwapchain(app);
            continue;
        }
        vkResetFences(app.device, 1, &app.inFlight[f]);

        VkCommandBuffer cb = app.commandBuffers[f];
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cb, &begin));
        VkClearValue clears[2]{};
        clears[0].color = {{0.05f, 0.05f, 0.07f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpBegin.renderPass = app.renderPass;
        rpBegin.framebuffer = app.framebuffers[imageIndex];
        rpBegin.renderArea = {{0, 0}, app.extent};
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = clears;
        vkCmdBeginRenderPass(cb, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app.pipeline);
        const VkViewport viewport{0, 0, (float)app.extent.width,
                                  (float)app.extent.height, 0, 1};
        const VkRect2D scissor{{0, 0}, app.extent};
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdPushConstants(cb, app.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &app.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cb, app.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cb, app.indexCount, 1, 0, 0, 0);
        vkCmdEndRenderPass(cb);
        VK_CHECK(vkEndCommandBuffer(cb));

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &app.imageAvailable[f];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cb;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &app.renderFinished[f];
        VK_CHECK(vkQueueSubmit(app.queue, 1, &submit, app.inFlight[f]));

        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &app.renderFinished[f];
        present.swapchainCount = 1;
        present.pSwapchains = &app.swapchain;
        present.pImageIndices = &imageIndex;
        const VkResult presented = vkQueuePresentKHR(app.queue, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            createSwapchain(app);
        }
        app.frame = (app.frame + 1) % kFramesInFlight;
    }

    vkDeviceWaitIdle(app.device);
    destroySwapchain(app);
    for (int i = 0; i < kFramesInFlight; i++) {
        vkDestroySemaphore(app.device, app.imageAvailable[i], nullptr);
        vkDestroySemaphore(app.device, app.renderFinished[i], nullptr);
        vkDestroyFence(app.device, app.inFlight[i], nullptr);
    }
    vkDestroyCommandPool(app.device, app.commandPool, nullptr);
    vkDestroyBuffer(app.device, app.vertexBuffer, nullptr);
    vkFreeMemory(app.device, app.vertexMemory, nullptr);
    vkDestroyBuffer(app.device, app.indexBuffer, nullptr);
    vkFreeMemory(app.device, app.indexMemory, nullptr);
    vkDestroyPipeline(app.device, app.pipeline, nullptr);
    vkDestroyPipelineLayout(app.device, app.pipelineLayout, nullptr);
    vkDestroyRenderPass(app.device, app.renderPass, nullptr);
    vkDestroyDevice(app.device, nullptr);
    vkDestroySurfaceKHR(app.instance, app.surface, nullptr);
    vkDestroyInstance(app.instance, nullptr);
    glfwDestroyWindow(app.window);
    glfwTerminate();
    return 0;
}
