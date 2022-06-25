/*
 * Source code for the NPGR019 lab practices. Copyright Martin Kahoun 2021.
 * Licensed under the zlib license, see LICENSE.txt in the root directory.
 */

#pragma once

#include <Camera.h>
#include <Geometry.h>
#include <Textures.h>

// Textures we'll be using
namespace LoadedTextures
{
  enum
  {
    White, Grey, Blue, CheckerBoard, Diffuse, Normal, Specular, Occlusion, NumTextures
  };
}

// Render mode structure
struct RenderSettings
{
  // Vsync on?
  bool vsync;
  // Draw wireframe?
  bool wireframe;
  // Tonemapping on?
  bool tonemapping;
  // Used MSAA samples
  GLsizei msaaLevel;
};

// Very simple scene abstraction class
class Scene
{
public:
  // Draw passes over the scene
  enum class RenderPass : int
  {
    // Single passes
    DepthPass = 0x0001,
    ShadowVolume = 0x0002,
    DirectLight = 0x0004,
    AmbientLight = 0x0008,
    // Combinations
    LightPass = 0x000c // diffuse | ambient
  };

  // Data for a single object instance
  struct InstanceData
  {
    // In this simple example just a transformation matrix, transposed for efficient storage
    glm::mat3x4 transformation;
  };

  // Maximum number of allowed instances - must match the instancing vertex shader!
  static const unsigned int MAX_INSTANCES = 1024;

  // Get and create instance for this singleton
  static Scene& GetInstance();
  // Initialize the test scene
  void Init(int numCubes, int numLights);
  // Updates positions
  void Update(float dt);
  void CreateDepthBuffer(int width, int height, int MSAA);
  // Draw the scene
  void Draw(const Camera &camera, const RenderSettings &renderMode, bool carmackReverse);
  // Return the generic VAO for rendering
  GLuint GetGenericVAO() { return _vao; }

private:
  // Structure describing light
  struct Light
  {
    // Position of the light
    glm::vec3 position;
    // Color and ambient intensity of the light
    glm::vec4 color;
    // Parameters for the light movement
    glm::vec4 movement;
  };

  struct SpotLight
  {
      // Position of the light
      glm::vec3 position;
      // Color and ambient intensity of the light
      glm::vec4 color;
      // Parameters for the light movement
      glm::vec4 movement;
      // normalized rotation vector?
      glm::vec3 lightDirection;
      // intensity inside is full
      glm::f32 innerLightAngleDegrees;
      // intensity outside the innerLightAngleDegrees but inside the outerLightAngleDegrees is with falloff
      glm::f32 outerLightAngleDegrees;
      // distance after which the light is not calculated
      glm::f32 lightDistance;
  };

  // All is private, instance is created in GetInstance()
  Scene();
  ~Scene();
  // No copies allowed
  Scene(const Scene &);
  Scene & operator = (const Scene &);

  // Helper function for binding the appropriate textures
  void BindTextures(const GLuint &diffuse, const GLuint &normal, const GLuint &specular, const GLuint &occlusion);
  // Helper function for creating and updating the instance data. Copies array of transforms for the cubes to our _instancingBuffer UBO
  void UpdateInstanceData();
  // Helper function for updating shader program data
  void UpdateProgramData(GLuint program, RenderPass renderPass, const Camera &camera, const glm::vec3 &lightPosition, const glm::vec4 &lightColor, const glm::vec3& lightDirection);
  void UpdateProgramDataSpotlights(GLuint program, RenderPass renderPass, const Camera& camera, const glm::vec3& lightPosition, const glm::vec4& lightColor, const glm::vec3& lightDirection, const glm::f32& innerAngleDegrees, const glm::f32& outerAngleDegrees, const glm::f32& maxLightDistance);
  // Helper method to update transformation uniform block
  void UpdateTransformBlock(const Camera &camera);
  // Draw the backdrop, floor and walls
  void DrawBackground(GLuint program, RenderPass renderPass, const Camera &camera, const glm::vec3 &lightPosition, const glm::vec4 &lightColor);
  void DrawBackgroundSpotlights(GLuint program, RenderPass renderPass, const Camera& camera, const glm::vec3& lightPosition, const glm::vec4& lightColor, const glm::vec3& lightDirection, const glm::f32& innerAngleDegrees, const glm::f32& outerAngleDegrees, const glm::f32& maxLightDistance);
  // Draw cubes
  void DrawObjects(GLuint program, RenderPass renderPass, const Camera &camera, const glm::vec3 &lightPosition, const glm::vec4 &lightColor);

  // Textures helper instance
  Textures &_textures;
  // Loaded textures
  GLuint _loadedTextures[LoadedTextures::NumTextures] = {0};
  // Number of cubes in the scene
  int _numCubes = 10;
  // Cube positions
  std::vector<glm::vec3> _cubePositions;
  // Number of point lights in the scene
  int _numPointLights;
  // Number of spot lights in the scene
  int _numSpotLights;
  // Lights positions
  std::vector<Light> _lights;
  // Lights positions
  std::vector<SpotLight> _spotLights;
  // General use VAO
  GLuint _vao = 0;
  // Quad instance
  Mesh<Vertex_Pos_Nrm_Tgt_Tex> *_quad = nullptr;
  // Cube instance
  Mesh<Vertex_Pos_Nrm_Tgt_Tex> *_cube = nullptr;
  // Cube instance w/ adjacency information
  Mesh<Vertex_Pos> *_cubeAdjacency = nullptr;
  // Instancing buffer handle
  GLuint _instancingBuffer = 0;
  // Transformation matrices uniform buffer object
  GLuint _transformBlockUBO = 0;
  // Depth maps for lights
  //std::vector<GLuint> lightDepthTextures;
  GLuint depthMapTexture;
  // FBO for depth map (texture)
  GLuint depthMapFBO = 0;
};
