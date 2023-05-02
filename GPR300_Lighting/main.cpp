#include "GL/glew.h"
#include "GLFW/glfw3.h"

#include <glm/glm.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stdio.h>

#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include "EW/Shader.h"
#include "EW/EwMath.h"
#include "EW/Camera.h"
#include "EW/Mesh.h"
#include "EW/Transform.h"
#include "EW/ShapeGen.h"

void processInput(GLFWwindow* window);
void resizeFrameBufferCallback(GLFWwindow* window, int width, int height);
void keyboardCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods);
void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
void mousePosCallback(GLFWwindow* window, double xpos, double ypos);
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

float lastFrameTime;
float deltaTime;

int SCREEN_WIDTH = 1080;
int SCREEN_HEIGHT = 720;

double prevMouseX;
double prevMouseY;
bool firstMouseInput = false;

/* Button to lock / unlock mouse
* 1 = right, 2 = middle
* Mouse will start locked. Unlock it to use UI
* */
const int MOUSE_TOGGLE_BUTTON = 1;
const float MOUSE_SENSITIVITY = 0.1f;
const float CAMERA_MOVE_SPEED = 5.0f;
const float CAMERA_ZOOM_SPEED = 3.0f;

Camera camera((float)SCREEN_WIDTH / (float)SCREEN_HEIGHT);

glm::vec3 bgColor = glm::vec3(0);
glm::vec3 lightColor = glm::vec3(1.0f);
glm::vec3 lightPosition = glm::vec3(0.0f, 3.0f, 0.0f);

bool wireFrame = false;

struct Light
{
	glm::vec3 position;
	glm::vec3 color;
	float intensity;
};

struct DirectionalLight
{
	glm::vec3 direction;
	Light light;
};

struct PointLight
{
	glm::vec3 position;
	Light light;

	float constK, linearK, quadraticK;
};

struct SpotLight
{
	glm::vec3 position;
	glm::vec3 direction;
	Light light;

	float range;
	float innerAngle;
	float outerAngle;
	float angleFalloff;
};

struct Material
{
	glm::vec3 color;
	float ambientK, diffuseK, specularK; // (0-1 range)
	float shininess = 1; // (1-512 range)
};

int numPointLights = 0;
glm::vec3 pointLightOrbitCenter;
float pointLightOrbitRange;
float pointLightOrbitSpeed;

DirectionalLight _DirectionalLight;
PointLight _PointLight;
SpotLight _SpotLight;
Material _Material;

GLuint getTexture(const char* texturePath)
{
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	int width, height, numComponents = 3;
	unsigned char* data = stbi_load(texturePath, &width, &height, &numComponents, 0);

	if (data != NULL)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	else
	{
		std::cout << "Failed." << std::endl;
	}

	stbi_image_free(data);

	return texture;
}

class FrameBuffer
{
public:
	FrameBuffer(int colorBuffers, int width, int height)
	{
		mWidth = width;
		mHeight = height;
		mTexturesLength = colorBuffers;
		textures = new unsigned int[mTexturesLength];

		glGenFramebuffers(1, &fbo);

		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		glGenTextures(mTexturesLength, textures);

		unsigned int* attachments = new unsigned int[mTexturesLength];

		for (int i = 0; i < mTexturesLength; i++)
		{
			glBindTexture(GL_TEXTURE_2D, textures[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D, 0);

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, textures[i], 0);

			attachments[i] = GL_COLOR_ATTACHMENT0 + i;
		}

		glGenRenderbuffers(1, &rbo);
		glBindRenderbuffer(GL_RENDERBUFFER, rbo);

		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, width, height);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

		glDrawBuffers(mTexturesLength, attachments);

		delete[] attachments;

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
		}

		else
		{
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	~FrameBuffer()
	{
		glDeleteRenderbuffers(1, &rbo);
		glDeleteTextures(mTexturesLength, textures);
		glDeleteFramebuffers(1, &fbo);

		delete[] textures;
		textures = nullptr;
	}

	unsigned int getFBO() { return fbo; }

	unsigned int getTexture(int texNum) { return textures[texNum]; }

private:
	unsigned int fbo;
	unsigned int* textures;
	unsigned int depth;
	unsigned int rbo;

	int mWidth, mHeight;
	int mTexturesLength;
};

class ShadowBuffer
{
public:
	ShadowBuffer(int width, int height)
	{
		mWidth = width;
		mHeight = height;

		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		glGenTextures(1, &depthTexture);
		glBindTexture(GL_TEXTURE_2D, depthTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, mWidth, mHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);

		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
		}

		else
		{
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	~ShadowBuffer()
	{
		
	}

	unsigned int getFBO() { return fbo; }
	unsigned int getTexture() { return depthTexture; }

private:
	unsigned int fbo;
	unsigned int depthTexture;

	int mWidth, mHeight;
};

/*
* My goal with this class is to allow for direct usage of
* the existing Mesh class without having to mess with any
* of its internal functions while also allowing for it to
* be used for instanced rendering.
* 
* Ideally this can be done with a seperate VBO for instance
* related vertex attribute loading, allowing for it to then
* be bound to the mesh's VAO before the instanced draw call.
* 
* This should also allow for direct modification of and access
* to instance data, potentially allowing for its modification
* at runtime.
*/
class InstancedMesh
{
public:

	/*
	* Generates an array buffer that serves the purpose
	* of storing vec3 offsets for each instance that should
	* be drawn. The space allocated is equivalent to that
	* of a set maximum amount of potential instances, but
	* not the amount that will be drawn at any given time.
	* 
	* The buffer is then bound to the fourth vertex attribute
	* of the given mesh's vertex array. glVertexAttribDivisor
	* specifies that the fourth attribute should be updated
	* for every instance that is drawn.
	*/
	InstancedMesh(ew::Transform transform, ew::MeshData data, int totalCount)
	{
		meshTransform = transform;
		meshData = data;
		mesh = new ew::Mesh(&meshData);
		glBindVertexArray(mesh->getVAO());

		instanceCount = 0;
		totalInstanceCount = totalCount;

		glGenBuffers(1, &instancedVBO);
		glBindBuffer(GL_ARRAY_BUFFER, instancedVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * totalInstanceCount, nullptr, GL_DYNAMIC_DRAW);

		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

		glVertexAttribDivisor(4, 1);
	}

	~InstancedMesh()
	{
		delete mesh;
	}

	/*
	* Performs an instanced draw call for
	* the amount of instances that should
	* be drawn.
	*/
	void draw()
	{
		glBindVertexArray(mesh->getVAO());

		glDrawElementsInstanced(GL_TRIANGLES, mesh->getNumIndicies(), GL_UNSIGNED_INT, 0, instanceCount);

		glBindVertexArray(0);
	}

	/*
	* Updates the data stored in the offset buffer
	*/
	void updateData(glm::vec3* dataVec, int instances)
	{
		glBindBuffer(GL_ARRAY_BUFFER, instancedVBO);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(glm::vec3) * instances, dataVec);
		instanceCount = instances;
	}

	/*
	* Updates data for a specific instance in the offset buffer
	*/
	void updateTargetData(glm::vec3* data, int instanceID)
	{
		if (instanceID > instanceCount) { return; }

		glBindBuffer(GL_ARRAY_BUFFER, instancedVBO);
		glBufferSubData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * instanceID, sizeof(glm::vec3), data);
	}

	glm::mat4 getModelMatrix() { return meshTransform.getModelMatrix(); }

private:
	ew::Transform meshTransform;
	ew::MeshData meshData;
	ew::Mesh* mesh;
	
	int instanceCount;
	int totalInstanceCount;
	unsigned int instancedVBO;
};

// Models
// Global for the sake of convenience
ew::Transform cubeTransform;
ew::Transform rectangleTransform;
ew::Transform sphereTransform;
ew::Transform planeTransform;
ew::Transform cylinderTransform;
ew::Transform quadTransform;
ew::Transform depthQuadTransform;
ew::Transform lightTransform;

ew::MeshData cubeMeshData;
ew::MeshData sphereMeshData;
ew::MeshData rectangleMeshData;
ew::MeshData planeMeshData;
ew::MeshData cylinderMeshData;
ew::MeshData quadMeshData;
ew::MeshData depthQuadMeshData;

ew::Mesh* cubeMesh;
ew::Mesh* sphereMesh;
ew::Mesh* rectangleMesh;
ew::Mesh* planeMesh;
ew::Mesh* cylinderMesh;
ew::Mesh* quadMesh;
ew::Mesh* depthQuadMesh;

InstancedMesh* instanced;

void drawScene(Shader& targetShader, glm::mat4 viewMatrix, glm::mat4 projectionMatrix)
{
	targetShader.setMat4("_View", viewMatrix);
	targetShader.setMat4("_Projection", projectionMatrix);

	targetShader.setMat4("_Model", cubeTransform.getModelMatrix());
	cubeMesh->draw();

	targetShader.setMat4("_Model", rectangleTransform.getModelMatrix());
	rectangleMesh->draw();

	targetShader.setMat4("_Model", sphereTransform.getModelMatrix());
	sphereMesh->draw();

	targetShader.setMat4("_Model", cylinderTransform.getModelMatrix());
	cylinderMesh->draw();

	targetShader.setMat4("_Model", planeTransform.getModelMatrix());
	planeMesh->draw();
}

/*
* Function that draws the scene using
* the instanced object.
*/
void drawSceneInstanced(Shader& targetShader, glm::mat4 viewMatrix, glm::mat4 projectionMatrix)
{
	targetShader.setMat4("_View", viewMatrix);
	targetShader.setMat4("_Projection", projectionMatrix);

	targetShader.setMat4("_Model", instanced->getModelMatrix());
	instanced->draw();
}

/*
* Updates a given array to assign positions
* that create a cube in shape.
*/
void buildScene(glm::vec3 offsets[], int instances)
{
	int cubed = cbrt(instances);

	for (int i = 0; i < cubed; i++)
	{
		for (int j = 0; j < cubed; j++)
		{
			for (int k = 0; k < cubed; k++)
			{
				offsets[(i * cubed * cubed) + (j * cubed) + k] = glm::vec3(i * 10, j * 10, k * 10);
			}
		}
	}
	instanced->updateData(offsets, instances);
}

int main() {
	if (!glfwInit()) {
		printf("glfw failed to init");
		return 1;
	}

	GLFWwindow* window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Lighting", 0, 0);
	glfwMakeContextCurrent(window);

	if (glewInit() != GLEW_OK) {
		printf("glew failed to init");
		return 1;
	}

	glfwSetFramebufferSizeCallback(window, resizeFrameBufferCallback);
	glfwSetKeyCallback(window, keyboardCallback);
	glfwSetScrollCallback(window, mouseScrollCallback);
	glfwSetCursorPosCallback(window, mousePosCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init();

	ImGui::StyleColorsDark();

	Shader litShader("shaders/defaultLit.vert", "shaders/defaultLit.frag");
	Shader unlitShader("shaders/defaultLit.vert", "shaders/unlit.frag");
	Shader depthOnly("shaders/depthOnly.vert", "shaders/depthOnly.frag");
	Shader postProc("shaders/postProcessing.vert", "shaders/postProcessing.frag");

	FrameBuffer screenBuffer = FrameBuffer(1, SCREEN_WIDTH, SCREEN_HEIGHT);

	ew::createCube(1.0f, 1.0f, 1.0f, cubeMeshData);
	ew::createCube(1.0f, 2.0f, 1.0f, rectangleMeshData);
	ew::createSphere(0.5f, 64, sphereMeshData);
	ew::createPlane(1.0f, 1.0f, planeMeshData);
	ew::createCylinder(1.0f, 0.5f, 64, cylinderMeshData);
	ew::createQuad(2.0f, 2.0f, quadMeshData);
	ew::createQuad(0.5f, 0.5f, depthQuadMeshData);

	cubeMesh = new ew::Mesh(&cubeMeshData);
	rectangleMesh = new ew::Mesh(&rectangleMeshData);
	sphereMesh = new ew::Mesh(&sphereMeshData);
	planeMesh = new ew::Mesh(&planeMeshData);
	cylinderMesh = new ew::Mesh(&cylinderMeshData);
	quadMesh = new ew::Mesh(&quadMeshData);
	depthQuadMesh = new ew::Mesh(&depthQuadMeshData);

	/*
	* Initialization of instanced rendering
	*/
	int instances = 1000000;
	const int MAX_INSTANCES = 1000000;
	glm::vec3* instanceOffsets = new glm::vec3[MAX_INSTANCES];
	instanced = new InstancedMesh(cubeTransform, cubeMeshData, MAX_INSTANCES);

	// Stores a target instance to be updated by the GUI
	int targetInstance = 0;

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	quadTransform.position = glm::vec3(0.0f, 0.0f, 0.0f);
	depthQuadTransform.position = glm::vec3(0.5f, 0.5f, 0.0f);

	cubeTransform.position = glm::vec3(-2.0f, 0.0f, 0.0f);
	rectangleTransform.position = glm::vec3(0.0f, 0.0f, -2.0f);
	sphereTransform.position = glm::vec3(0.0f, 0.0f, 0.0f);

	planeTransform.position = glm::vec3(0.0f, -1.0f, 0.0f);
	planeTransform.scale = glm::vec3(10.0f);

	cylinderTransform.position = glm::vec3(2.0f, 0.0f, 0.0f);

	lightTransform.scale = glm::vec3(0.5f);
	lightTransform.position = glm::vec3(0.0f, 5.0f, 0.0f);

	_Material.color = glm::vec3(1, 1, 1);
	_Material.ambientK = 1;
	_Material.diffuseK = 1;
	_Material.specularK = 1;
	_Material.shininess = 1;

	_DirectionalLight.direction = glm::vec3(2, 2, 2);
	_DirectionalLight.light.intensity = 0.5f;
	_DirectionalLight.light.color = glm::vec3(1, 1, 1);

	float minBias = 0.000f;
	float maxBias = 0.001f;
	bool showShadowMap = false;

	const char* effectNames[5] = { "None", "Invert", "Red Overlay", "Zooming Out", "Wave"};
	int effectIndex = 0;

	GLuint brickTexture = getTexture("Bricks.jpg");
	GLuint tileTexture = getTexture("Tiles.jpg");
	GLuint brickNormal = getTexture("BricksNormal.jpg");

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, brickTexture);
	litShader.setInt("_Texture1", 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, tileTexture);
	litShader.setInt("_Texture2", 1);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, brickNormal);
	litShader.setInt("_Normal", 2);

	buildScene(instanceOffsets, instances);

	while (!glfwWindowShouldClose(window)) {

		processInput(window);
		glClearColor(bgColor.r,bgColor.g,bgColor.b, 1.0f);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		float time = (float)glfwGetTime();
		deltaTime = time - lastFrameTime;
		lastFrameTime = time;

		glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
		glBindFramebuffer(GL_FRAMEBUFFER, screenBuffer.getFBO());

		glEnable(GL_DEPTH_TEST);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		litShader.use();

		litShader.setFloat("time", time);

		litShader.setVec3("_DirectionalLight.direction", _DirectionalLight.direction);
		litShader.setFloat("_DirectionalLight.light.intensity", _DirectionalLight.light.intensity);
		litShader.setVec3("_DirectionalLight.light.color", _DirectionalLight.light.color);

		litShader.setVec3("_Material.color", _Material.color);
		litShader.setFloat("_Material.ambientK", _Material.ambientK);
		litShader.setFloat("_Material.diffuseK", _Material.diffuseK);
		litShader.setFloat("_Material.specularK", _Material.specularK);
		litShader.setFloat("_Material.shininess", _Material.shininess);

		litShader.setVec3("_CameraPosition", camera.getPosition());
		
		litShader.setFloat("_MinBias", minBias);
		litShader.setFloat("_MaxBias", maxBias);

		glCullFace(GL_BACK);

		drawSceneInstanced(litShader, camera.getViewMatrix(), camera.getProjectionMatrix());

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glDisable(GL_DEPTH_TEST);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		postProc.use();

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, screenBuffer.getTexture(0));
		postProc.setInt("_Texture1", 4);

		postProc.setInt("effectIndex", effectIndex);
		postProc.setFloat("time", time);

		postProc.setMat4("_Model", quadTransform.getModelMatrix());
		quadMesh->draw();

		ImGui::Begin("Directional Light");

		ImGui::DragFloat3("Direction", &_DirectionalLight.direction.x, 1, -360, 360);
		ImGui::DragFloat("Intensity", &_DirectionalLight.light.intensity, 0.01, 0.01, 1);
		ImGui::ColorEdit3("Color", &_DirectionalLight.light.color.r);
		ImGui::End();

		ImGui::Begin("Post Processing");

		ImGui::Combo("Effects", &effectIndex, effectNames, IM_ARRAYSIZE(effectNames));
		ImGui::End();

		// This needs to be improved. ie. Have it so that data that corresponds
		// with instances that don't exist don't get updated.
		ImGui::Begin("Instancing");

		ImGui::InputInt("Target Instance", &targetInstance);
		ImGui::DragFloat3("Instance Position", &instanceOffsets[targetInstance].x, 0.1);
		if (ImGui::Button("Update Position"))
		{
			if (targetInstance > instances - 1) { targetInstance = instances - 1; }

			instanced->updateTargetData(&instanceOffsets[targetInstance], targetInstance);
		}

		ImGui::InputInt("Instance Count", &instances);
		if (ImGui::Button("Generate Instances"))
		{
			if (instances > MAX_INSTANCES) { instances = MAX_INSTANCES; }
			buildScene(instanceOffsets, instances);
		}
		ImGui::End();

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwPollEvents();

		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}
//Author: Eric Winebrenner
void resizeFrameBufferCallback(GLFWwindow* window, int width, int height)
{
	SCREEN_WIDTH = width;
	SCREEN_HEIGHT = height;
	camera.setAspectRatio((float)SCREEN_WIDTH / SCREEN_HEIGHT);
	glViewport(0, 0, width, height);
}
//Author: Eric Winebrenner
void keyboardCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods)
{
	if (keycode == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, true);
	}
	//Reset camera
	if (keycode == GLFW_KEY_R && action == GLFW_PRESS) {
		camera.setPosition(glm::vec3(0, 0, 5));
		camera.setYaw(-90.0f);
		camera.setPitch(0.0f);
		firstMouseInput = false;
	}
	if (keycode == GLFW_KEY_1 && action == GLFW_PRESS) {
		wireFrame = !wireFrame;
		glPolygonMode(GL_FRONT_AND_BACK, wireFrame ? GL_LINE : GL_FILL);
	}
}
//Author: Eric Winebrenner
void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	if (abs(yoffset) > 0) {
		float fov = camera.getFov() - (float)yoffset * CAMERA_ZOOM_SPEED;
		camera.setFov(fov);
	}
}
//Author: Eric Winebrenner
void mousePosCallback(GLFWwindow* window, double xpos, double ypos)
{
	if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
		return;
	}
	if (!firstMouseInput) {
		prevMouseX = xpos;
		prevMouseY = ypos;
		firstMouseInput = true;
	}
	float yaw = camera.getYaw() + (float)(xpos - prevMouseX) * MOUSE_SENSITIVITY;
	camera.setYaw(yaw);
	float pitch = camera.getPitch() - (float)(ypos - prevMouseY) * MOUSE_SENSITIVITY;
	pitch = glm::clamp(pitch, -89.9f, 89.9f);
	camera.setPitch(pitch);
	prevMouseX = xpos;
	prevMouseY = ypos;
}
//Author: Eric Winebrenner
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	//Toggle cursor lock
	if (button == MOUSE_TOGGLE_BUTTON && action == GLFW_PRESS) {
		int inputMode = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED;
		glfwSetInputMode(window, GLFW_CURSOR, inputMode);
		glfwGetCursorPos(window, &prevMouseX, &prevMouseY);
	}
}

//Author: Eric Winebrenner
//Returns -1, 0, or 1 depending on keys held
float getAxis(GLFWwindow* window, int positiveKey, int negativeKey) {
	float axis = 0.0f;
	if (glfwGetKey(window, positiveKey)) {
		axis++;
	}
	if (glfwGetKey(window, negativeKey)) {
		axis--;
	}
	return axis;
}

//Author: Eric Winebrenner
//Get input every frame
void processInput(GLFWwindow* window) {

	float moveAmnt = CAMERA_MOVE_SPEED * deltaTime;

	//Get camera vectors
	glm::vec3 forward = camera.getForward();
	glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0,1,0)));
	glm::vec3 up = glm::normalize(glm::cross(forward, right));

	glm::vec3 position = camera.getPosition();
	position += forward * getAxis(window, GLFW_KEY_W, GLFW_KEY_S) * moveAmnt;
	position += right * getAxis(window, GLFW_KEY_D, GLFW_KEY_A) * moveAmnt;
	position += up * getAxis(window, GLFW_KEY_Q, GLFW_KEY_E) * moveAmnt;
	camera.setPosition(position);
}
