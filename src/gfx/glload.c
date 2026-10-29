#include "gfx/glload.h"
#include "core/log.h"
VGL vgl;

#define LOAD(name) do { *(void (**)(void))&vgl.name = glfwGetProcAddress("gl" #name); \
    if (!vgl.name) { VLOG("missing OpenGL function gl" #name); return 0; } } while (0)

int vgl_load(void) {
    LOAD(CreateShader); LOAD(ShaderSource); LOAD(CompileShader); LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog); LOAD(CreateProgram); LOAD(AttachShader); LOAD(LinkProgram);
    LOAD(GetProgramiv); LOAD(GetProgramInfoLog); LOAD(UseProgram); LOAD(DeleteShader);
    LOAD(GetUniformLocation); LOAD(UniformMatrix4fv); LOAD(Uniform3f); LOAD(Uniform1f);
    LOAD(GenVertexArrays); LOAD(BindVertexArray); LOAD(GenBuffers); LOAD(BindBuffer);
    LOAD(BufferData); LOAD(VertexAttribPointer); LOAD(EnableVertexAttribArray);
    LOAD(Uniform1i); LOAD(Uniform4f); LOAD(ActiveTexture); LOAD(TexImage3D);
    LOAD(GenerateMipmap);
    return 1;
}
