#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <tuple>
#include <optional>
#include <map>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cstdlib>

/*
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
*/

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_ALIGNED_GENTYPES

#include <glm/glm.hpp>

#include <vulkan/vulkan.h>

#include <image.hpp>
#include <bvh.hpp>

using namespace vrt;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) 
{
	std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
	return VK_FALSE;
}

//Crude temporary error macro
#define PANIC_BAD_RESULT(result) \
	if (result != VK_SUCCESS) { std::cerr << "PANIC AT:" << __LINE__ << " " << __FILE__ << "\n"; exit(-1); }

class NonCopiable {
public:
	NonCopiable() = default;
	NonCopiable(NonCopiable const&) = delete;
	NonCopiable& operator=(NonCopiable const&) = delete;
};

class NonMovable {
public:
	NonMovable() = default;
	NonMovable(NonMovable&&) = delete;
	NonMovable& operator=(NonMovable&&) = delete;
};

class Buffer : public NonCopiable //Restrict Vk objects creation/destruction
{
public:
	//Make private const
	VkBuffer mBuffer;
	VkDeviceMemory mDeviceMemory;
	uint32_t mBufferSize;

	//Init to null (probaly could be defaulted)
	Buffer() : mBuffer(VK_NULL_HANDLE), mDeviceMemory(VK_NULL_HANDLE) {}
	Buffer(VkDevice device, VkPhysicalDevice physDevice, uint32_t queueFamilyIndex, 
		VkBufferUsageFlags usage, uint32_t bufferSize) 
	{
		init(device, physDevice, queueFamilyIndex, usage, bufferSize);
	}

	~Buffer()
	{
		if (mBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(mDevice, mBuffer, nullptr);
			vkFreeMemory(mDevice, mDeviceMemory, nullptr);
		}
	}

	Buffer& operator=(Buffer&& rhs)
	{
		if (mBuffer != rhs.mBuffer) {
			std::memcpy(this, &rhs, sizeof(Buffer));
			std::memset(&rhs, 0, sizeof(Buffer)); // Leave in a valid state
		}

		return *this;
	}

	Buffer(Buffer&& in) 
	{
		*this = std::move(in);
	}

	void map(VkDeviceSize offset, VkDeviceSize size, void** ptr)
	{
		PANIC_BAD_RESULT(vkMapMemory(mDevice, mDeviceMemory, offset, size, 0, ptr));
	}

	void unMap()
	{
		vkUnmapMemory(mDevice, mDeviceMemory);
	}

	//Use Vk objects until other systems are done
	void init(VkDevice device, VkPhysicalDevice physDevice, uint32_t queueFamilyIndex, 
		VkBufferUsageFlags usage, uint32_t bufferSize)
	{
		mDevice = device;
		mBufferSize = bufferSize;

		VkPhysicalDeviceMemoryProperties memoryProperties;
		vkGetPhysicalDeviceMemoryProperties(physDevice, &memoryProperties);

		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.queueFamilyIndexCount = 1;
		bufferInfo.pQueueFamilyIndices = &queueFamilyIndex;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; //TODO support for concurrent
		bufferInfo.usage = usage;
		bufferInfo.size = mBufferSize;	

		PANIC_BAD_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &mBuffer));

		// Use until memory manager is done
		VkMemoryRequirements memoryRequirements;
		vkGetBufferMemoryRequirements(device, mBuffer, &memoryRequirements);

		uint32_t memoryTypeIndex = 0;
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
			if (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT &&
				memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) 
				memoryTypeIndex = i;

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		allocInfo.allocationSize = memoryRequirements.size;

		PANIC_BAD_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &mDeviceMemory));
		PANIC_BAD_RESULT(vkBindBufferMemory(device, mBuffer, mDeviceMemory, 0));
	}
private:
	VkDevice mDevice;
};

class DescriptorSet : public NonCopiable{
public:
	// TODO Make private
	VkDescriptorSetLayout mLayout;
	VkDescriptorSet mSet;
	std::vector<VkDescriptorSetLayoutBinding> mBindings; // Descriptor count of a binding

	DescriptorSet() = default;
	DescriptorSet(VkDevice device, VkDescriptorSetLayout layout, VkDescriptorSet set, 
		std::vector<VkDescriptorSetLayoutBinding> const& bindings) :
		mDevice(device),
		mLayout(layout), 
		mSet(set), 
		mBindings(bindings)
	{

	}
	~DescriptorSet()
	{
		if (mLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(mDevice, mLayout, nullptr);
	}

	DescriptorSet& operator=(DescriptorSet&& rhs)
	{
		if (this->mLayout != rhs.mLayout) {
			std::memcpy(this, &rhs, sizeof(DescriptorSet));
			std::memset(&rhs, 0, sizeof(DescriptorSet));
		}
		return *this;
	}

	DescriptorSet(DescriptorSet&& in)
	{
		*this = std::move(in);
	}

	void update(uint32_t binding, uint32_t startElement, uint32_t descriptorCount, 
		uint32_t offset, uint64_t range, Buffer const& buffer)
	{
		auto itr = std::find_if(mBindings.begin(), mBindings.end(), 
			[&](VkDescriptorSetLayoutBinding b) -> bool { return b.binding == binding; });
		if (itr == mBindings.end()) {
			std::cerr << "Failed to update descriptors" << std::endl;
			return;
		}

		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = buffer.mBuffer;
		bufferInfo.offset = offset;
		bufferInfo.range = range;

		// TODO batch update
		VkWriteDescriptorSet writeSet = {};
		writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeSet.descriptorType = itr->descriptorType;
		writeSet.descriptorCount = descriptorCount; 
		writeSet.dstBinding = binding;
		writeSet.dstArrayElement = startElement;
		writeSet.dstSet = mSet;
		writeSet.pBufferInfo = &bufferInfo; 
		writeSet.pImageInfo = nullptr; // TODO
		writeSet.pTexelBufferView = nullptr; // TODO

		vkUpdateDescriptorSets(mDevice, 1, &writeSet, 0, nullptr);
	}
private:
	VkDevice mDevice;
};

class DescriptorPool : public NonCopiable {
public:
	DescriptorPool() = default;
	DescriptorPool(VkDevice device, std::vector<VkDescriptorPoolSize> const& sizes) :
		mSizes(sizes), mDevice(device)
	{
		init();
	}

	~DescriptorPool()
	{
		if (mPool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(mDevice, mPool, nullptr);
	}
	
	DescriptorPool& operator=(DescriptorPool&& rhs)
	{
		if (this->mPool != rhs.mPool) {
			std::memcpy(this, &rhs, sizeof(DescriptorPool));
			std::memset(&rhs, 0, sizeof(DescriptorPool));
		}
		return *this;
	}

	DescriptorPool(DescriptorPool&& in)
	{
		*this = std::move(in);
	}

	DescriptorSet createSet(std::vector<VkDescriptorSetLayoutBinding> const& bindings)
	{
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindings.size();
		layoutInfo.pBindings = bindings.data();

		VkDescriptorSetLayout setLayout;
		PANIC_BAD_RESULT(vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &setLayout));

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = mPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &setLayout;

		// TODO if possible, pre allocate descriptors 
		VkDescriptorSet descriptorSet;
		PANIC_BAD_RESULT(vkAllocateDescriptorSets(mDevice, &allocInfo, &descriptorSet));

		return {mDevice, setLayout, descriptorSet, bindings};
	}
private:;
	void init()
	{
		uint32_t maxSets = 0;
		for (auto& itr : mSizes)
			maxSets += itr.descriptorCount;

		VkDescriptorPoolCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		createInfo.maxSets = maxSets;	
		createInfo.poolSizeCount = mSizes.size();
		createInfo.pPoolSizes = mSizes.data();

		vkCreateDescriptorPool(mDevice, &createInfo, nullptr, &mPool);
	}

	VkDescriptorPool mPool;
	std::vector<VkDescriptorPoolSize> mSizes;
	VkDevice mDevice;
};

constexpr glm::vec3 WORLD_UP = glm::vec3(0.0, 1.0, 0.0);

struct Camera {
	alignas(16) glm::vec3 mPos;
	alignas(16) glm::vec3 mUp;
	alignas(16) glm::vec3 mForward;
	alignas(16) glm::vec3 mRight;

	Camera() = default;
	Camera(glm::vec3 const& pos,
		glm::vec3 const& forward,
		glm::vec3 const& up) :
		mPos(pos),
		mForward(forward),
		mUp(up)
	{
		mRight = glm::cross(up, forward);
	}

	void lookAt(glm::vec3 const& pos, glm::vec3 const& center)
	{
		mForward = glm::normalize(center - pos);
		mPos = pos;

		mRight = glm::cross(mForward, WORLD_UP);
		mUp = glm::normalize(glm::cross(mForward, mRight));
		mRight = glm::cross(mUp, mForward);
	}
};

class ComputeApp {
	VkInstance instance;

	VkPhysicalDevice physDevice;
	VkDevice device;
	VkQueue queue;

	Buffer imageBuffer;
	Buffer uniformBuffer;

	// BVH buffers
	// Buffer vertexBuffer; TODO
	Buffer nodeBuffer;
	Buffer refBuffer;

	Mesh mesh;

	DescriptorPool descriptorPool;
	DescriptorSet descriptorSet;

	Camera cam;

	VkShaderModule shader;
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;

	VkDebugUtilsMessengerEXT debugMessenger;

	bool useValidationLayers;
	uint32_t queueFamilyIndex;

	uint32_t imageW, imageH;

	void createDebugMessenger()
	{
		auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) 
		vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

		auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT) 
		vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

		VkDebugUtilsMessengerCreateInfoEXT debugUtils = {};
		debugUtils.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugUtils.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
									VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
									VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugUtils.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugUtils.pfnUserCallback = debugCallback;

		vkCreateDebugUtilsMessengerEXT(instance, &debugUtils, nullptr, &debugMessenger);
	}

	void createInstance() 
	{
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "BVH test app";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "Null Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_1;

		std::vector<const char*> extensionNames;
		const std::vector<const char*> validationLayers = {
			"VK_LAYER_LUNARG_standard_validation"
		};

		VkInstanceCreateInfo InstanceInfo = {};

		bool validationOk = false;
		if (useValidationLayers) {
			uint32_t layerPropertiesCount = 0;
			vkEnumerateInstanceLayerProperties(&layerPropertiesCount, nullptr);
			std::vector<VkLayerProperties> layerProperties(layerPropertiesCount);
			vkEnumerateInstanceLayerProperties(&layerPropertiesCount, layerProperties.data());

			for (auto const& layer : layerProperties) 
				if (strcmp(layer.layerName, validationLayers[0]) == 0) 
					validationOk = true;
				
			if (validationOk)
				std::cout << "Validation layer ok" << std::endl;
			
			InstanceInfo.enabledLayerCount = validationLayers.size();
			InstanceInfo.ppEnabledLayerNames = validationLayers.data();

			extensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		InstanceInfo.pApplicationInfo = &appInfo;
		InstanceInfo.enabledExtensionCount = extensionNames.size();
		InstanceInfo.ppEnabledExtensionNames = extensionNames.data();

		vkCreateInstance(&InstanceInfo, nullptr, &instance);
		if (instance == VK_NULL_HANDLE) {
			std::cout << "bleb\n";
		}

		if (useValidationLayers && validationOk)
			createDebugMessenger();
	}

	void createDeviceAndQueue()
	{
		// For now use the first result
		uint32_t physDeviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &physDeviceCount, nullptr);
		vkEnumeratePhysicalDevices(instance, &physDeviceCount, &physDevice);

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> deviceProperties(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &queueFamilyCount, 
												deviceProperties.data());

		// For now select a queue with compute
		for (unsigned i = 0; i < deviceProperties.size(); ++i) {
			auto const& queueFamily = deviceProperties[i];
			if (queueFamily.queueCount > 0 && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)) {
				queueFamilyIndex = i;
				std::cout << "Found queue family\n";
				break;
			}
		}

		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo deviceQueueInfo = {};
		deviceQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		deviceQueueInfo.queueFamilyIndex = queueFamilyIndex;
		deviceQueueInfo.queueCount = 1;
		deviceQueueInfo.pQueuePriorities = &queuePriority;

		VkDeviceCreateInfo deviceInfo = {};
		deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &deviceQueueInfo;

		vkCreateDevice(physDevice, &deviceInfo, nullptr, &device);
		vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
	}

	void createBuffers()
	{
		imageW = 800;
		imageH = 600;
		uint32_t bufferSize = sizeof(Pixel) * imageH * imageW;
		void* data;

		mesh = loadMesh("bunny.obj").value();
		std::vector<BVHTriangleRef> refList = buildTriangleRefList(mesh.triangles, mesh.vertex_data);	
		BVHBuildNode* buildNode = buildBVHNode(refList);
		BVH bvh;
		buildBVH(buildNode, bvh);

		cam.lookAt(glm::vec3(0.02, 0.2, 0.2), glm::vec3(0.0, 0.1, 0.0));

		imageBuffer.init(device, physDevice, queueFamilyIndex, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, bufferSize);
		uniformBuffer.init(device, physDevice, queueFamilyIndex, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(Camera));
		refBuffer.init(device, physDevice, queueFamilyIndex, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			sizeof(BVHTriangleRef) * bvh.refList.size() + sizeof(uint32_t));
		nodeBuffer.init(device, physDevice, queueFamilyIndex, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
			sizeof(BVHNode) * bvh.nodeList.size() + sizeof(uint32_t));

		imageBuffer.map(0, 32, &data);
		*((uint32_t*)data  ) = imageW;
		*((uint32_t*)data+1) = imageH;
		imageBuffer.unMap();

		uniformBuffer.map(0, sizeof(Camera), &data);
		*(Camera*)data = cam;
		uniformBuffer.unMap();

		refBuffer.map(0, VK_WHOLE_SIZE, &data);
		*((uint32_t*)data) = bvh.refList.size();
		std::memcpy(((char*)data+16), bvh.refList.data(), bvh.refList.size()*sizeof(BVHTriangleRef));
		refBuffer.unMap();

		nodeBuffer.map(0, VK_WHOLE_SIZE, &data);
		*((uint32_t*)data) = bvh.nodeList.size();
		std::memcpy(((char*)data+16), bvh.nodeList.data(), bvh.nodeList.size()*sizeof(BVHNode));
		nodeBuffer.unMap();
	}

	void createDescriptors()
	{
		std::vector<VkDescriptorPoolSize> sizes;
		sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3});
		sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1});

		descriptorPool = DescriptorPool(device, sizes);

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		bindings.push_back({0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
		bindings.push_back({1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
		bindings.push_back({2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
		bindings.push_back({3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});

		descriptorSet = descriptorPool.createSet(bindings);

		descriptorSet.update(0, 0, 1, 0, VK_WHOLE_SIZE, imageBuffer);
		descriptorSet.update(1, 0, 1, 0, VK_WHOLE_SIZE, uniformBuffer);
		descriptorSet.update(2, 0, 1, 0, VK_WHOLE_SIZE, refBuffer);
		descriptorSet.update(3, 0, 1, 0, VK_WHOLE_SIZE, nodeBuffer);

	}

	void createShader()
	{
		uint32_t codeSize;
		std::vector<char> computeCode;
		std::ifstream fs("shaders/compute.comp.spv", std::ios::binary | std::ios::ate);

		// This is just a complete mess
		if (!fs.is_open()) {
			std::cerr << "Failed to open shader file" << std::endl;
		} else {
			codeSize = fs.tellg();
			fs.seekg(0);
			computeCode.resize(codeSize);
			fs.read(computeCode.data(), codeSize);
		}

		VkShaderModuleCreateInfo shaderInfo = {};
		shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		shaderInfo.codeSize = codeSize;
		shaderInfo.pCode = (const uint32_t*)computeCode.data();

		vkCreateShaderModule(device, &shaderInfo, nullptr, &shader);
	}

	void createPipeline()
	{
		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &descriptorSet.mLayout;
		vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);

		VkPipelineShaderStageCreateInfo shaderStageInfo = {};
		shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		shaderStageInfo.pName = "main";
		shaderStageInfo.module = shader;

		VkComputePipelineCreateInfo computeInfo = {};
		computeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		computeInfo.stage = shaderStageInfo;
		computeInfo.layout = pipelineLayout;

		vkCreateComputePipelines(device, 0, 1, &computeInfo, nullptr, &pipeline);
	}

	void createCommandBuffer()
	{
		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndex;

		vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
	}

	void recordCommandBuffer()
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1,
			&descriptorSet.mSet, 0, NULL);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

		vkCmdDispatch(commandBuffer, (uint32_t)std::ceil(imageW / 16.0f), 
									(uint32_t)std::ceil(imageH / 16.0f), 1);

		vkEndCommandBuffer(commandBuffer);
	}

public:
	ComputeApp(bool useValidationLayers) : 
	useValidationLayers(useValidationLayers)
	{
	}

	void cleanUp()
	{
		vkDestroyCommandPool(device, commandPool, nullptr);
	}

	void init()
	{
		createInstance();
		createDeviceAndQueue();
		createBuffers();
		createDescriptors();
		createShader();
		createPipeline();
		createCommandBuffer();

		recordCommandBuffer();
	}

	void run()
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pCommandBuffers = &commandBuffer;
		submitInfo.commandBufferCount = 1;

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

		VkFence fence;
		vkCreateFence(device, &fenceInfo, nullptr, &fence);

		vkQueueSubmit(queue, 1, &submitInfo, fence);

		vkWaitForFences(device, 1, &fence, VK_TRUE, 100000000000);

		vkDestroyFence(device, fence, nullptr);
	}

	void saveResult()
	{
		Pixel* data;

		vkMapMemory(device, imageBuffer.mDeviceMemory, 0, imageBuffer.mBufferSize, 0, (void**)(&data));

		std::vector<Pixel> pixels(data, data + (imageW * imageH));
		Image image(imageW, imageH, pixels);

		savePPMImage(image, "out.ppm");

		vkUnmapMemory(device, imageBuffer.mDeviceMemory);
	}
};

void traverse(BVHBuildNode* node)
{
	if (!node->isLeaf) {
		traverse(node->left);
		traverse(node->right);
	} else {
		std::cout << node->refList.size() << std::endl;
	}
}

int main(int argc, char **argv)
{
	ComputeApp app(true);
	app.init();
	app.run();
	app.saveResult();
	
  	return 0;
}