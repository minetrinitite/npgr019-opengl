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
#include <glm/gtx/string_cast.hpp>

#include <MathSupport.h>

#define GLM_ENABLE_EXPERIMENTAL

// Scaling factor for lights movement curve
static const glm::vec3 scale = glm::vec3(13.0f, 2.0f, 13.0f);
// Offset for lights movement curve
static const glm::vec3 offset = glm::vec3(0.0f, 3.0f, 0.0f);

static const glm::vec3 spotlightsAnimationScale = glm::vec3(6.0f, 1.5f, 6.0f);
static const glm::vec3 spotlightsAnimationOffset = glm::vec3(0.5f, 2.0f, 0.5f);

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
  _numSpotLights = 1;
  _numPointLights = numLights;
  

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
    // Generate the UBO (e.g. constant) handle with transform matrix
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
  const float ambientIntensity = 1e-3f / numLights;

  // Position & color of the first light
  _lights.reserve(_numPointLights);
  glm::vec4 nextPosition = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
  _lights.push_back({glm::vec3(-3.0f, 3.0f, 0.0f), glm::vec4(10.0f, 10.0f, 10.0f, ambientIntensity), nextPosition});

  // Create Spot Light as the 2nd
  _spotLights.reserve(_numSpotLights);
  nextPosition = glm::vec4(2.0f, 1.0f, 1.0f, 0.0f);
  glm::vec3 spotLightDirection = glm::vec3(0.5f, -0.5f, 0.0f);
  glm::f32 innerAngle = 30.0f;
  glm::f32 outerAngle = 40.0f;
  glm::f32 dropoffDistance = 5000.0f;

  _spotLights.push_back({ glm::vec3(-3.0f, 2.0f, 0.0f), glm::vec4(10.0f, 10.0f, 10.0f, ambientIntensity), nextPosition, 
                          spotLightDirection, innerAngle, outerAngle, dropoffDistance});

  // Generate random positions for the rest of the lights
  for (int i = 1; i < _numPointLights; ++i)
  {
    float x = getRandom(-2.0f, 2.0f);
    float y = getRandom(-2.0f, 2.0f);
    float z = getRandom(-2.0f, 2.0f);
    float w = getRandom(-2.0f, 2.0f);
    glm::vec4 nextPosition = glm::vec4(x, y, z, w);

    float r = getRandom(0.0f, 5.0f);
    float g = getRandom(0.0f, 5.0f);
    float b = getRandom(0.0f, 5.0f);
    glm::vec4 color = glm::vec4(r, g, b, ambientIntensity);

    _lights.push_back({offset + lissajous(nextPosition, 0.0f) * scale, color, nextPosition});
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

  // same for the spotlight
  _spotLights[0].position = glm::vec3(-1.5f, 0.2f, 0.5f) + lissajous(_spotLights[0].movement, t);

  // Update the rest of the lights
  for (int i = 1; i < _numPointLights; ++i)
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
  // Called each frame
  // we prepare the data inside the 
  
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
  // e.g. copy the required data (transforms of cubes) for instancing to the UBO (max. size 16kB)
  void *ptr = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
  memcpy(ptr, &*instanceData.begin(), _numCubes * sizeof(InstanceData));
  glUnmapBuffer(GL_UNIFORM_BUFFER);

  // Unbind the instancing buffer
  glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0);
}

void Scene::UpdateProgramData(GLuint program, RenderPass renderPass, const Camera &camera, const glm::vec3 &lightPosition,
    const glm::vec4 &lightColor, const glm::vec3 &lightDirection)
{
  // Update the light position, use 4th component to pass direct light intensity
  if ((int)renderPass & ((int)RenderPass::ShadowVolume | (int)RenderPass::LightPass))
  {
    GLint lightLoc = glGetUniformLocation(program, "lightPosWS");
    glUniform4f(lightLoc, lightPosition.x, lightPosition.y, lightPosition.z, ((int)renderPass & (int)RenderPass::DirectLight) ? 1.0f : 0.0f);
  }

  if ((int)renderPass & ((int)RenderPass::DepthPass))
  {
      glm::mat4 lightView = glm::lookAt(lightPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
      glm::mat4 lightMatrix = camera.GetProjection() * lightView;
      //printf("%f\n  %f\n    %f\n      %f\n", lightMatrix[0].x, lightMatrix[1].y, lightMatrix[2].z, lightMatrix[3].w);
      GLint lightMatrixLoc = glGetUniformLocation(program, "lightMatrix");
      glUniformMatrix4fv(lightMatrixLoc, 1, GL_FALSE, &lightMatrix[0][0]);
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

void Scene::UpdateProgramDataSpotlights(GLuint program, RenderPass renderPass, const Camera& camera, const glm::vec3& lightPosition, const glm::vec4& lightColor,
    const glm::vec3& lightDirection, const glm::f32& innerAngleDegrees, const glm::f32& outerAngleDegrees, const glm::f32& maxLightDistance)
{
    // Update the light position, use 4th component to pass direct light intensity
    if ((int)renderPass & ((int)RenderPass::ShadowVolume | (int)RenderPass::LightPass))
    {
        GLint lightLoc = glGetUniformLocation(program, "lightPosWS");
        glUniform4f(lightLoc, lightPosition.x, lightPosition.y, lightPosition.z, ((int)renderPass & (int)RenderPass::DirectLight) ? 1.0f : 0.0f);
    }

    if ((int)renderPass & ((int)RenderPass::DepthPass))
    {
        glm::mat4 lightView = glm::lookAt(lightPosition, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightMatrix = camera.GetProjection() * lightView;
        //printf("%f\n  %f\n    %f\n      %f\n", lightMatrix[0].x, lightMatrix[1].y, lightMatrix[2].z, lightMatrix[3].w);
        GLint lightMatrixLoc = glGetUniformLocation(program, "lightMatrix");
        glUniformMatrix4fv(lightMatrixLoc, 1, GL_FALSE, &lightMatrix[0][0]);
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
    GLint lightDirectionLoc = glGetUniformLocation(program, "lightDirection");
    GLint innerAngleDegreesLoc = glGetUniformLocation(program, "innerAngleDegrees");
    GLint outerAngleDegreesLoc = glGetUniformLocation(program, "outerAngleDegrees");
    GLint maxLightDistanceLoc = glGetUniformLocation(program, "maxLightDistance");
    glUniform3fv(lightDirectionLoc, 1, glm::value_ptr(lightDirection));
    glUniform1f(innerAngleDegreesLoc, innerAngleDegrees);
    glUniform1f(outerAngleDegreesLoc, outerAngleDegrees);
    glUniform1f(maxLightDistanceLoc, maxLightDistance);

}

void Scene::UpdateProgramDataSpotlightsShadow(GLuint program, RenderPass renderPass, const Camera& camera, const glm::vec3& lightPosition, const glm::vec4& lightColor, const glm::vec3& lightDirection, const glm::f32& innerAngleDegrees, const glm::f32& outerAngleDegrees, const glm::f32& maxLightDistance)
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
    GLint lightDirectionLoc = glGetUniformLocation(program, "lightDirection");
    GLint innerAngleDegreesLoc = glGetUniformLocation(program, "innerAngleDegrees");
    GLint outerAngleDegreesLoc = glGetUniformLocation(program, "outerAngleDegrees");
    GLint maxLightDistanceLoc = glGetUniformLocation(program, "maxLightDistance");
    glUniform3fv(lightDirectionLoc, 1, glm::value_ptr(lightDirection));
    glUniform1f(innerAngleDegreesLoc, innerAngleDegrees);
    glUniform1f(outerAngleDegreesLoc, outerAngleDegrees);
    glUniform1f(maxLightDistanceLoc, maxLightDistance);
}

void Scene::UpdateTransformBlock(const Camera &camera)
{
  // Tell OpenGL we want to work with our transform block
  // It is binded to all (4) shaders
  glBindBuffer(GL_UNIFORM_BUFFER, _transformBlockUBO);

  // uniform TransformBlock:
  // mat3x4 worldToView;
  // mat4x4 projection;

  // Note: we should properly obtain block members size and offset via
  // glGetActiveUniformBlockiv() with GL_UNIFORM_SIZE, GL_UNIFORM_OFFSET,
  // I'm yoloing it here...

  // Update the world to view transformation matrix - transpose to 3 columns, 4 rows for storage in an uniform block:
  // per std140 layout column matrix CxR is stored as an array of C columns with R elements, i.e., 4x3 matrix would
  // waste space because it would require padding to vec4
  glm::mat3x4 worldToView = glm::transpose(camera.GetWorldToView());
  // globally change UBO
  // glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
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
  glm::vec3 dummyLightDirection = glm::vec3(0.0f);
  UpdateProgramData(program, renderPass, camera, lightPosition, lightColor, dummyLightDirection);

  // Bind textures
  // RenderPass::LightPass will accept DirectLight and AmbientLight
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
  glBindVertexArray(0);
}

void Scene::DrawBackgroundSpotlights(GLuint program, RenderPass renderPass, const Camera& camera, const glm::vec3& lightPosition, const glm::vec4& lightColor,
    const glm::vec3& lightDirection, const glm::f32& innerAngleDegrees, const glm::f32& outerAngleDegrees, const glm::f32& maxLightDistance)
{
    // Bind the shader program and update its data
    glUseProgram(program);
    UpdateProgramDataSpotlights(program, renderPass, camera, lightPosition, lightColor, lightDirection, innerAngleDegrees, outerAngleDegrees, maxLightDistance);

    // Bind textures
    if ((int)renderPass)
    {
        BindTextures(_loadedTextures[LoadedTextures::CheckerBoard], _loadedTextures[LoadedTextures::Blue], _loadedTextures[LoadedTextures::Grey], _loadedTextures[LoadedTextures::White]);
        glActiveTexture(GL_TEXTURE0 + 4);
        glBindTexture(GL_TEXTURE_2D, depthMapTexture);
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
  glm::vec3 dummyLightDirection = glm::vec3(0.0f);
  UpdateProgramData(program, renderPass, camera, lightPosition, lightColor, dummyLightDirection);

  // Bind the instancing buffer to the index 1
  glBindBufferBase(GL_UNIFORM_BUFFER, 1, _instancingBuffer);

  // Bind textures
  if ((int)renderPass & (int)RenderPass::LightPass)
  {
    BindTextures(_loadedTextures[LoadedTextures::Diffuse], _loadedTextures[LoadedTextures::Normal], _loadedTextures[LoadedTextures::Specular], _loadedTextures[LoadedTextures::Occlusion]);
  }

  // Draw cubes
    glBindVertexArray(_cube->GetVAO());
    glDrawElementsInstanced(GL_TRIANGLES, _cube->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0), _numCubes);

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
  glBindVertexArray(0);
}

void Scene::DrawObjectsSpotlightsShadow(GLuint program, RenderPass renderPass, const Camera& camera, const glm::vec3& lightPosition, const glm::vec4& lightColor,
    const glm::vec3& lightDirection, const glm::f32& innerAngleDegrees, const glm::f32& outerAngleDegrees, const glm::f32& maxLightDistance)
{
    // Bind the shader program and update its data
    glUseProgram(program);
    UpdateProgramDataSpotlights(program, renderPass, camera, lightPosition, lightColor, lightDirection, innerAngleDegrees, outerAngleDegrees, maxLightDistance);

    // Bind the instancing buffer to the index 1
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, _instancingBuffer);

    // Bind textures
    if ((int)renderPass & (int)RenderPass::LightPass)
    {
        BindTextures(_loadedTextures[LoadedTextures::Diffuse], _loadedTextures[LoadedTextures::Normal], _loadedTextures[LoadedTextures::Specular], _loadedTextures[LoadedTextures::Occlusion]);
        glActiveTexture(GL_TEXTURE0 + 4);
        glBindTexture(GL_TEXTURE_2D, depthMapTexture);
    }

    // Draw cubes
    glBindVertexArray(_cube->GetVAO());
    glDrawElementsInstanced(GL_TRIANGLES, _cube->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0), _numCubes);

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
        //glDisable(GL_BLEND);

        glPointSize(10.0f);
        glBindVertexArray(_vao);
        glDrawArrays(GL_POINTS, 0, 1);
    }
}

void Scene::Draw(const Camera &camera, const RenderSettings &renderMode, bool carmackReverse)
{
  UpdateTransformBlock(camera);

  // --------------------------------------------------------------------------
  // Light pass drawing:
  // --------------------------------------------------------------------------
  // lambda
  auto lightPass = [this, &renderMode, &camera](RenderPass renderPass, const glm::vec3 &lightPosition, const glm::vec4 &lightColor)
  {
    // Enable additive alpha blending
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);   
    glBlendFunc(GL_ONE, GL_ONE);

    // draws background
    DrawBackground(shaderProgram[ShaderProgram::Default], renderPass, camera, lightPosition, lightColor);
    // draws the color (material) of objects
    DrawObjects(shaderProgram[ShaderProgram::Instancing], renderPass, camera, lightPosition, lightColor);

    // Disable blending after this pass
    glDisable(GL_BLEND);
  };

  // --------------------------------------------------------------------------

  // --------------------------------------------------------------------------
  // Spot Lights pass drawing:
  // --------------------------------------------------------------------------
  auto spotLightPass = [this, &renderMode, &camera](
      RenderPass renderPass,
      const glm::vec3& lightPosition,
      const glm::vec4& lightColor,
      const glm::vec3& lightDirection,
      const glm::f32& innerAngleDegrees,
      const glm::f32& outerAngleDegrees,
      const glm::f32& maxLightDistance
      )
  {
      // Enable additive alpha blending
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ONE, GL_ONE);

      // draws background
      float innerAngleCos = glm::cos(glm::radians(innerAngleDegrees));
      float outerAngleCos = glm::cos(glm::radians(outerAngleDegrees));
      DrawBackgroundSpotlights(shaderProgram[ShaderProgram::SpotlightDefault], renderPass, camera, lightPosition, lightColor, lightDirection,
          innerAngleCos, outerAngleCos, maxLightDistance);
      // draws the color (material) of objects
      //DrawObjects(shaderProgram[ShaderProgram::Instancing], renderPass, camera, lightPosition, lightColor);
      DrawObjectsSpotlightsShadow(shaderProgram[ShaderProgram::InstancingSpotlightShadow], renderPass, camera, lightPosition, lightColor, lightDirection,
          innerAngleCos, outerAngleCos, maxLightDistance);

      // Disable blending after this pass
      glDisable(GL_BLEND);
  };

  // --------------------------------------------------------------------------
  // Shadow Map pass drawing:
  // --------------------------------------------------------------------------
  auto shadowMapSpotlightPass = [this, &renderMode, &camera](const glm::vec3& lightPosition, const glm::vec4& lightColor, const glm::vec3& lightDirection)
  {
      // Enable additive alpha blending
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ONE, GL_ONE);
      //DrawObjects(shaderProgram[ShaderProgram::InstancedShadowVolume], camera, lightPosition, lightColor);
      //DrawObjects(shaderProgram[ShaderProgram::InstancingDepthPass], RenderPass::DepthPass, camera, dummyLightPosition, dummyColor);
      // Bind the shader program and update its data
      glUseProgram(shaderProgram[ShaderProgram::InstancingDepthPass]);
      // Update the transformation & projection matrices
      Camera cameraAtLightPos = Camera(camera);
      cameraAtLightPos.SetTransformation(lightPosition, lightDirection, glm::vec3(0.0f, 1.0f, 0.0f));
      //cameraAtLightPos.SetProjection(glm::radians(90.0f), ((float)screenWidth)/((float)screenHeight), camera.GetNearClip(), camera.GetFarClip());
      UpdateProgramData(shaderProgram[ShaderProgram::InstancingDepthPass], RenderPass::DepthPass, cameraAtLightPos, lightPosition, lightColor, lightDirection);

      // Bind depth buffer for rendering the depth into it
      // TODO: the fact that we have to remember the previous FB means that the depth buffer is used in the wrong order. Should be used earlier
      GLint previousFramebufferId = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebufferId);
      glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
      glClearDepth(0.5);
      glClear(GL_DEPTH_BUFFER_BIT);
      glClearDepth(1);

      // Bind the instancing buffer to the index 1
      glBindBufferBase(GL_UNIFORM_BUFFER, 1, _instancingBuffer);

      // Use default cube VAO and GL_TRIANGLES
      glBindVertexArray(_cube->GetVAO());
      glDrawElementsInstanced(GL_TRIANGLES, _cube->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0), _numCubes);

      // Unbind the instancing buffer
      glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0);
      // Unbind the depth framebuffer
      glBindFramebuffer(GL_FRAMEBUFFER, previousFramebufferId);
      // Disable blending after this pass
      glDisable(GL_BLEND);
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
  // enable write into Depth Buffer
  glDepthMask(GL_TRUE);

  // Clear the color and depth buffer
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Note: for depth primed geometry, it would be the best option to also set depth function to GL_EQUAL
 

  // first depth buffer
  for (int i = 0; i < _numSpotLights; ++i)
  {
      // This should fill the depth buffer (texture)
      glColorMask(false, false, false, false);
      shadowMapSpotlightPass(_spotLights[i].position, _spotLights[i].color, _spotLights[i].lightDirection);
  }
  for (int i = 0; i < _numPointLights; ++i)
  {
      // This should fill the depth buffer (texture)
      // Waiting until start to implement Point Lights (Omnidirectional Shadow Maps)
      //shadowMapSpotlightPass(_spotLights[i].position, _spotLights[i].color, _spotLights[i].lightDirection);
  }

  // For each light we need to render the scene with its contribution
  for (int i = 0; i < _numPointLights; ++i)
  {
    // Draw direct light, enable color write
    glColorMask(true, true, true, true);
    lightPass(RenderPass::DirectLight, _lights[i].position, _lights[i].color);

    //lightPass(RenderPass::AmbientLight, _lights[i].position, _lights[i].color);
  }

  for (int i = 0; i < _numSpotLights; ++i)
  {
      // Draw direct light, enable color write
      glColorMask(true, true, true, true);
      //spotLightPass(RenderPass::DirectLight, _spotLights[i].position, _spotLights[i].color, _spotLights[i].lightDirection, _spotLights[i].innerLightAngleDegrees,
      //    _spotLights[i].outerLightAngleDegrees, _spotLights[i].lightDistance);

      //spotLightPass(RenderPass::AmbientLight, _spotLights[i].position, _spotLights[i].color, _spotLights[i].lightDirection, _spotLights[i].innerLightAngleDegrees,
      //    _spotLights[i].outerLightAngleDegrees, _spotLights[i].lightDistance);
  }

  // Don't forget to leave the color write enabled
  glColorMask(true, true, true, true);
}

void Scene::CreateDepthBuffer(int width, int height, int MSAA = 0)
{
    // Depth Buffer
    if (!depthMapFBO)
    {
        printf("creating depth buffer \n");
        glGenFramebuffers(1, &depthMapFBO);
    }

    // prepare depth buffer (texture) for the shadow map
    glGenTextures(1, &depthMapTexture);
    glBindTexture(GL_TEXTURE_2D, depthMapTexture);
    // Allocate storage for the texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    // Set up default filtering modes
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Set up depth comparison mode
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    // Set up wrapping modes
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Set up done, unbind
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMapTexture, 0);

    // We only need single channel (W) for depth, so we will not use RGB, thererore explicitly turn off color buffer
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    // Check for completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("Failed to create depth framebuffer: 0x%04X\n", status);
    }
    // Set up done, unbind
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}