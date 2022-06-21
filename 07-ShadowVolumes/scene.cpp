/*
 * Source code for the NPGR019 lab practices. Copyright Martin Kahoun 2021.
 * Licensed under the zlib license, see LICENSE.txt in the root directory.
 */

#include "scene.h"
#include "shaders.h"

#include <vector>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include <MathSupport.h>

// Scaling factor for lights movement curve
static const glm::vec3 scale = glm::vec3(13.0f, 2.0f, 13.0f);
// Offset for lights movement curve
static const glm::vec3 offset = glm::vec3(0.0f, 3.0f, 0.0f);

// Lissajous curve position calculation based on the parameters
auto lissajous = [](const glm::vec4 &p, float t) -> glm::vec3
{
  return glm::vec3(sinf(p.x * t), cosf(p.y * t), sinf(p.z * t) * cosf(p.w * t));
};

// ----------------------------------------------------------------------------

Scene& Scene::GetInstance()
{
  static Scene scene;
  return scene;
}

Scene::Scene() : _textures(Textures::GetInstance())
{

}

Scene::~Scene()
{
  // Delete meshes
  delete _quad;
  _quad = nullptr;
  delete _cube;
  _cube = nullptr;
  delete _cubeAdjacency;
  _cubeAdjacency = nullptr;

  // Release the instancing buffer
  glDeleteBuffers(1, &_instancingBuffer);

  // Release the generic VAO
  glDeleteVertexArrays(1, &_vao);

  // Release textures
  glDeleteTextures(LoadedTextures::NumTextures, _loadedTextures);
}

void Scene::Init(int numCubes, int numLights)
{
  // Check if already initialized and return
  if (_vao)
    return;

  _numCubes = numCubes;
  _numLights = numLights;

  // Prepare meshes
  _quad = Geometry::CreateQuadNormalTangentTex();
  _cube = Geometry::CreateCubeNormalTangentTex();
  _cubeAdjacency = Geometry::CreateCubeAdjacency();

  // Create general use VAO
  glGenVertexArrays(1, &_vao);

  {
    // Generate the instancing buffer as Uniform Buffer Object
    glGenBuffers(1, &_instancingBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, _instancingBuffer);

    // Obtain UBO index and size from the instancing shader program
    GLuint uboIndex = glGetUniformBlockIndex(shaderProgram[ShaderProgram::Instancing], "InstanceBuffer");
    GLint uboSize = 0;
    glGetActiveUniformBlockiv(shaderProgram[ShaderProgram::Instancing], uboIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &uboSize);

    // Describe the buffer data - we're going to change this every frame
    glBufferData(GL_UNIFORM_BUFFER, uboSize, nullptr, GL_DYNAMIC_DRAW);

    // Unbind the GL_UNIFORM_BUFFER target for now
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
  }

  {
    // Generate the transform UBO handle
    glGenBuffers(1, &_transformBlockUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, _transformBlockUBO);

    // Obtain UBO index from the default shader program:
    // we're gonna bind this UBO for all shader programs and we're making
    // assumption that all of the UBO's used by our shader programs are
    // all the same size
    GLuint uboIndex = glGetUniformBlockIndex(shaderProgram[ShaderProgram::Default], "TransformBlock");
    GLint uboSize = 0;
    glGetActiveUniformBlockiv(shaderProgram[ShaderProgram::Default], uboIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &uboSize);

    // Describe the buffer data - we're going to change this every frame
    glBufferData(GL_UNIFORM_BUFFER, uboSize, nullptr, GL_DYNAMIC_DRAW);

    // Bind the memory for usage
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, _transformBlockUBO);

    // Unbind the GL_UNIFORM_BUFFER target for now
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
  }

  // --------------------------------------------------------------------------

  // Position the first cube half a meter above origin
  _cubePositions.reserve(_numCubes);
  _cubePositions.push_back(glm::vec3(0.0f, 0.5f, 0.0f));

  // Generate random positions for the rest of the cubes
  for (int i = 1; i < _numCubes; ++i)
  {
    float x = getRandom(-5.0f, 5.0f);
    float y = getRandom( 1.0f, 5.0f);
    float z = getRandom(-5.0f, 5.0f);

    _cubePositions.push_back(glm::vec3(x, y, z));
  }

  // --------------------------------------------------------------------------

  // Ambient intensity for the lights
  const float ambientIntentsity = 1e-3f / numLights;

  // Position & color of the first light
  _lights.reserve(_numLights);
  glm::vec4 p = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
  _lights.push_back({glm::vec3(-3.0f, 3.0f, 0.0f), glm::vec4(10.0f, 10.0f, 10.0f, ambientIntentsity), p});

  // Generate random positions for the rest of the lights
  for (int i = 1; i < _numLights; ++i)
  {
    float x = getRandom(-2.0f, 2.0f);
    float y = getRandom(-2.0f, 2.0f);
    float z = getRandom(-2.0f, 2.0f);
    float w = getRandom(-2.0f, 2.0f);
    glm::vec4 p = glm::vec4(x, y, z, w);

    float r = getRandom(0.0f, 5.0f);
    float g = getRandom(0.0f, 5.0f);
    float b = getRandom(0.0f, 5.0f);
    glm::vec4 c = glm::vec4(r, g, b, ambientIntentsity);

    _lights.push_back({offset + lissajous(p, 0.0f) * scale, c, p});
  }

  // --------------------------------------------------------------------------

  // Create texture samplers
  _textures.CreateSamplers();

  // Prepare textures
  _loadedTextures[LoadedTextures::White] = Textures::CreateSingleColorTexture(255, 255, 255);
  _loadedTextures[LoadedTextures::Grey] = Textures::CreateSingleColorTexture(127, 127, 127);
  _loadedTextures[LoadedTextures::Blue] = Textures::CreateSingleColorTexture(127, 127, 255);
  _loadedTextures[LoadedTextures::CheckerBoard] = Textures::CreateCheckerBoardTexture(256, 16);
  _loadedTextures[LoadedTextures::Diffuse] = Textures::LoadTexture("data/Terracotta_Tiles_002_Base_Color.jpg", true);
  _loadedTextures[LoadedTextures::Normal] = Textures::LoadTexture("data/Terracotta_Tiles_002_Normal.jpg", false);
  _loadedTextures[LoadedTextures::Specular] = Textures::LoadTexture("data/Terracotta_Tiles_002_Roughness.jpg", false);
  _loadedTextures[LoadedTextures::Occlusion] = Textures::LoadTexture("data/Terracotta_Tiles_002_ambientOcclusion.jpg", false);
}

void Scene::Update(float dt)
{
  // Animation timer
  static float t = 0.0f;

  // Treat the first light as a special case with offset
  _lights[0].position = glm::vec3(-3.0f, 2.0f, 0.0f) + lissajous(_lights[0].movement, t);

  // Update the rest of the lights
  for (int i = 1; i < _numLights; ++i)
  {
    _lights[i].position = offset + lissajous(_lights[i].movement, t) * scale;
  }

  // Update the animation timer
  t += dt;
}

void Scene::BindTextures(const GLuint &diffuse, const GLuint &normal, const GLuint &specular, const GLuint &occlusion)
{
  // We want to bind textures and appropriate samplers
  glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, diffuse);
  glBindSampler(0, _textures.GetSampler(Sampler::Anisotropic));

  glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D, normal);
  glBindSampler(1, _textures.GetSampler(Sampler::Anisotropic));

  glActiveTexture(GL_TEXTURE0 + 2);
  glBindTexture(GL_TEXTURE_2D, specular);
  glBindSampler(2, _textures.GetSampler(Sampler::Anisotropic));

  glActiveTexture(GL_TEXTURE0 + 3);
  glBindTexture(GL_TEXTURE_2D, occlusion);
  glBindSampler(3, _textures.GetSampler(Sampler::Anisotropic));
}

void Scene::UpdateInstanceData()
{
  // Create transformation matrix
  glm::mat4x4 transformation = glm::mat4x4(1.0f);
  // Instance data CPU side buffer
  static std::vector<InstanceData> instanceData(MAX_INSTANCES);

  // Cubes
  float angle = 20.0f;
  for (int i = 0; i < _numCubes; ++i)
  {
    // Create unit matrix
    transformation = glm::translate(_cubePositions[i]);
    transformation *= glm::rotate(glm::radians(i * angle), glm::vec3(1.0f, 1.0f, 1.0f));

    instanceData[i].transformation = glm::transpose(transformation);
  }

  // Bind the instancing buffer to the index 1
  glBindBufferBase(GL_UNIFORM_BUFFER, 1, _instancingBuffer);

  // Update the buffer data using mapping
  void *ptr = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
  memcpy(ptr, &*instanceData.begin(), _numCubes * sizeof(InstanceData));
  glUnmapBuffer(GL_UNIFORM_BUFFER);

  // Unbind the instancing buffer
  glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0);
}

void Scene::UpdateProgramData(GLuint program, RenderPass renderPass, const Camera &camera, const glm::vec3 &lightPosition, const glm::vec4 &lightColor)
{
  // Update the light position, use 4th component to pass direct light intensity
  if ((int)renderPass & ((int)RenderPass::ShadowVolume | (int)RenderPass::LightPass))
  {
    GLint lightLoc = glGetUniformLocation(program, "lightPosWS");
    glUniform4f(lightLoc, lightPosition.x, lightPosition.y, lightPosition.z, ((int)renderPass & (int)RenderPass::DirectLight) ? 1.0f : 0.0f);
  }

  // Update view position and light color
  if ((int)renderPass & (int)RenderPass::LightPass)
  {
    // Update the view position
    GLint viewPosLoc = glGetUniformLocation(program, "viewPosWS");
    glm::vec4 viewPos = camera.GetViewToWorld()[3];
    glUniform4fv(viewPosLoc, 1, glm::value_ptr(viewPos));

    // Update the light color, 4th component controls ambient light intensity
    GLint lightColorLoc = glGetUniformLocation(program, "lightColor");
    glUniform4f(lightColorLoc, lightColor.x, lightColor.y, lightColor.z, ((int)renderPass & (int)RenderPass::AmbientLight) ? lightColor.w : 0.0f);
  }
}

void Scene::UpdateTransformBlock(const Camera &camera)
{
  // Tell OpenGL we want to work with our transform block
  glBindBuffer(GL_UNIFORM_BUFFER, _transformBlockUBO);

  // Note: we should properly obtain block members size and offset via
  // glGetActiveUniformBlockiv() with GL_UNIFORM_SIZE, GL_UNIFORM_OFFSET,
  // I'm yoloing it here...

  // Update the world to view transformation matrix - transpose to 3 columns, 4 rows for storage in an uniform block:
  // per std140 layout column matrix CxR is stored as an array of C columns with R elements, i.e., 4x3 matrix would
  // waste space because it would require padding to vec4
  glm::mat3x4 worldToView = glm::transpose(camera.GetWorldToView());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat3x4), static_cast<const void*>(&*glm::value_ptr(worldToView)));

  // Update the projection matrix
  glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat3x4), sizeof(glm::mat4x4), static_cast<const void*>(&*glm::value_ptr(camera.GetProjection())));

  // Unbind the GL_UNIFORM_BUFFER target for now
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void Scene::DrawBackground(GLuint program, RenderPass renderPass, const Camera &camera, const glm::vec3 &lightPosition, const glm::vec4 &lightColor)
{
  // Bind the shader program and update its data
  glUseProgram(program);
  UpdateProgramData(program, renderPass, camera, lightPosition, lightColor);

  // Bind textures
  if ((int)renderPass & (int)RenderPass::LightPass)
  {
    BindTextures(_loadedTextures[LoadedTextures::CheckerBoard], _loadedTextures[LoadedTextures::Blue], _loadedTextures[LoadedTextures::Grey], _loadedTextures[LoadedTextures::White]);
  }

  // Bind the geometry
  glBindVertexArray(_quad->GetVAO());

  // Draw floor:
  glm::mat4x4 transformation = glm::scale(glm::vec3(30.0f, 1.0f, 30.0f));
  glm::mat4x3 passMatrix = transformation;
  glUniformMatrix4x3fv(0, 1, GL_FALSE, glm::value_ptr(passMatrix));
  glDrawElements(GL_TRIANGLES, _quad->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0));

  // Draw Z axis wall
  transformation = glm::translate(glm::vec3(0.0f, 0.0f, 15.0f));
  transformation *= glm::rotate(-PI_HALF, glm::vec3(1.0f, 0.0f, 0.0f));
  transformation *= glm::scale(glm::vec3(30.0f, 1.0f, 30.0f));
  passMatrix = transformation;
  glUniformMatrix4x3fv(0, 1, GL_FALSE, glm::value_ptr(passMatrix));
  glDrawElements(GL_TRIANGLES, _quad->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0));

  // Draw X axis wall
  transformation = glm::translate(glm::vec3(15.0f, 0.0f, 0.0f));
  transformation *= glm::rotate(PI_HALF, glm::vec3(0.0f, 0.0f, 1.0f));
  transformation *= glm::scale(glm::vec3(30.0f, 1.0f, 30.0f));
  passMatrix = transformation;
  glUniformMatrix4x3fv(0, 1, GL_FALSE, glm::value_ptr(passMatrix));
  glDrawElements(GL_TRIANGLES, _quad->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0));
}

void Scene::DrawObjects(GLuint program, RenderPass renderPass, const Camera &camera, const glm::vec3 &lightPosition, const glm::vec4 &lightColor)
{
  // Bind the shader program and update its data
  glUseProgram(program);
  // Update the transformation & projection matrices
  UpdateProgramData(program, renderPass, camera, lightPosition, lightColor);

  // Bind the instancing buffer to the index 1
  glBindBufferBase(GL_UNIFORM_BUFFER, 1, _instancingBuffer);

  // Bind textures
  if ((int)renderPass & (int)RenderPass::LightPass)
  {
    BindTextures(_loadedTextures[LoadedTextures::Diffuse], _loadedTextures[LoadedTextures::Normal], _loadedTextures[LoadedTextures::Specular], _loadedTextures[LoadedTextures::Occlusion]);
  }

  // Draw cubes based on the renderPass
  if ((int)renderPass & (int)RenderPass::ShadowVolume)
  {
    // For shadow volumes we need to render using the GL_TRIANGLES_ADJACENCY mode and appropriate geometry
    glBindVertexArray(_cubeAdjacency->GetVAO());
    glDrawElementsInstanced(GL_TRIANGLES_ADJACENCY, _cubeAdjacency->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0), _numCubes);
  }
  else
  {
    // All other passes can use default cube VAO and GL_TRIANGLES
    glBindVertexArray(_cube->GetVAO());
    glDrawElementsInstanced(GL_TRIANGLES, _cube->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0), _numCubes);
  }

  // Unbind the instancing buffer
  glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0);

  // --------------------------------------------------------------------------

  // Draw the light object during the ambient pass
  if ((int)renderPass & (int)RenderPass::AmbientLight)
  {
    glUseProgram(shaderProgram[ShaderProgram::PointRendering]);

    // Update the light position
    GLint loc = glGetUniformLocation(shaderProgram[ShaderProgram::PointRendering], "position");
    glUniform3fv(loc, 1, glm::value_ptr(lightPosition));

    // Update the color
    loc = glGetUniformLocation(shaderProgram[ShaderProgram::PointRendering], "color");
    glUniform3fv(loc, 1, glm::value_ptr(lightColor * 0.05f));

    // Disable blending for lights
    glDisable(GL_BLEND);

    glPointSize(10.0f);
    glBindVertexArray(_vao);
    glDrawArrays(GL_POINTS, 0, 1);
  }
}

void Scene::Draw(const Camera &camera, const RenderMode &renderMode, bool carmackReverse)
{
  UpdateTransformBlock(camera);

  // --------------------------------------------------------------------------
  // Depth pass drawing:
  // --------------------------------------------------------------------------
  auto depthPass = [this, &renderMode, &camera]()
  {
    // No need to pass real light position and color as we don't need them in the depth pass
    DrawBackground(shaderProgram[ShaderProgram::DefaultDepthPass], RenderPass::DepthPass, camera, glm::vec3(0.0f), glm::vec4(0.0f));
    DrawObjects(shaderProgram[ShaderProgram::InstancingDepthPass], RenderPass::DepthPass, camera, glm::vec3(0.0f), glm::vec4(0.0f));
  };

  // --------------------------------------------------------------------------
  // Light pass drawing:
  // --------------------------------------------------------------------------
  auto lightPass = [this, &renderMode, &camera](RenderPass renderPass, const glm::vec3 &lightPosition, const glm::vec4 &lightColor)
  {
    // Enable additive alpha blending
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);

    // Pass only if equal to 0, i.e., outside shadow volume
    glStencilFunc(GL_EQUAL, 0x00, 0xff);

    // Don't update the stencil buffer
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    DrawBackground(shaderProgram[ShaderProgram::Default], renderPass, camera, lightPosition, lightColor);
    DrawObjects(shaderProgram[ShaderProgram::Instancing], renderPass, camera, lightPosition, lightColor);

    // Disable blending after this pass
    glDisable(GL_BLEND);
  };

  // --------------------------------------------------------------------------
  // Shadow pass drawing:
  // --------------------------------------------------------------------------
  auto shadowPass = [this, &renderMode, &camera, &carmackReverse](const glm::vec3 &lightPosition, const glm::vec4 &lightColor)
  {
    // Disable face culling
    glDisable(GL_CULL_FACE);

    // Always pass the stencil test
    glStencilFunc(GL_ALWAYS, 0x00, 0xff);

    if (carmackReverse)
    {
      // Set stencil operations for depth fail algorithm (licensed)
      // arguments: face, stencil fail, depth fail, depth pass
      glStencilOpSeparate(GL_BACK, GL_KEEP, GL_INCR_WRAP, GL_KEEP);
      glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_DECR_WRAP, GL_KEEP);
    }
    else
    {
      // Set stencil operations for depth pass algorithm
      // arguments: face, stencil fail, depth fail, depth pass
      glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
      glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    }

    DrawObjects(shaderProgram[ShaderProgram::InstancedShadowVolume], RenderPass::ShadowVolume, camera, lightPosition, lightColor);

    // Enable it back again
    glEnable(GL_CULL_FACE);
  };

  // --------------------------------------------------------------------------

  // Update the scene
  UpdateInstanceData();

  // Enable/disable MSAA rendering
  if (renderMode.msaaLevel > 1)
    glEnable(GL_MULTISAMPLE);
  else
    glDisable(GL_MULTISAMPLE);

  // Enable backface culling
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  // Enable/disable wireframe
  glPolygonMode(GL_FRONT_AND_BACK, renderMode.wireframe ? GL_LINE : GL_FILL);

  // Enable depth test, clamp, and write
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_DEPTH_CLAMP);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);

  // Clear the color and depth buffer
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Render the scene into the depth buffer only, disable color write
  glColorMask(false, false, false, false);
  depthPass();

  // We primed the depth buffer, no need to write to it anymore
  // Note: for depth primed geometry, it would be the best option to also set depth function to GL_EQUAL
  glDepthMask(GL_FALSE);

  // For each light we need to render the scene with its contribution
  for (int i = 0; i < _numLights; ++i)
  {
    // Enable stencil test and clear the stencil buffer
    glClear(GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);

    // Draw shadow volumes first, disable color write
    glColorMask(false, false, false, false);
    shadowPass(_lights[i].position, _lights[i].color);

    // Draw direct light utilizing stenciled shadows, enable color write
    glColorMask(true, true, true, true);
    lightPass(RenderPass::DirectLight, _lights[i].position, _lights[i].color);

    // Disable stencil test as we don't want shadows to affect ambient light
    glDisable(GL_STENCIL_TEST);
    lightPass(RenderPass::AmbientLight, _lights[i].position, _lights[i].color);
  }

  // Don't forget to leave the color write enabled
  glColorMask(true, true, true, true);
}
