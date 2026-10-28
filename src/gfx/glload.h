#ifndef VIPER_GLLOAD_H
#define VIPER_GLLOAD_H
/* Minimal OpenGL 3.3 function loader (no external generator needed).
   GL 1.1 calls (glClear, glDrawArrays...) come from the system GL header via GLFW. */
#include <stddef.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define VGL_CALL __stdcall
#else
#define VGL_CALL
#endif
#define VGL_FRAGMENT_SHADER 0x8B30
#define VGL_VERTEX_SHADER 0x8B31
#define VGL_COMPILE_STATUS 0x8B81
#define VGL_LINK_STATUS 0x8B82
#define VGL_ARRAY_BUFFER 0x8892
#define VGL_STATIC_DRAW 0x88E4
#define VGL_MULTISAMPLE 0x809D
#define VGL_TEXTURE_3D 0x806F
#define VGL_TEXTURE0 0x84C0

typedef struct {
    unsigned (VGL_CALL *CreateShader)(unsigned);
    void (VGL_CALL *ShaderSource)(unsigned, int, const char *const *, const int *);
    void (VGL_CALL *CompileShader)(unsigned);
    void (VGL_CALL *GetShaderiv)(unsigned, unsigned, int *);
    void (VGL_CALL *GetShaderInfoLog)(unsigned, int, int *, char *);
    unsigned (VGL_CALL *CreateProgram)(void);
    void (VGL_CALL *AttachShader)(unsigned, unsigned);
    void (VGL_CALL *LinkProgram)(unsigned);
    void (VGL_CALL *GetProgramiv)(unsigned, unsigned, int *);
    void (VGL_CALL *GetProgramInfoLog)(unsigned, int, int *, char *);
    void (VGL_CALL *UseProgram)(unsigned);
    void (VGL_CALL *DeleteShader)(unsigned);
    int (VGL_CALL *GetUniformLocation)(unsigned, const char *);
    void (VGL_CALL *UniformMatrix4fv)(int, int, unsigned char, const float *);
    void (VGL_CALL *Uniform3f)(int, float, float, float);
    void (VGL_CALL *Uniform1f)(int, float);
    void (VGL_CALL *Uniform1i)(int, int);
    void (VGL_CALL *Uniform4f)(int, float, float, float, float);
    void (VGL_CALL *ActiveTexture)(unsigned);
    void (VGL_CALL *TexImage3D)(unsigned, int, int, int, int, int, int, unsigned, unsigned, const void *);
    void (VGL_CALL *GenVertexArrays)(int, unsigned *);
    void (VGL_CALL *BindVertexArray)(unsigned);
    void (VGL_CALL *GenBuffers)(int, unsigned *);
    void (VGL_CALL *BindBuffer)(unsigned, unsigned);
    void (VGL_CALL *BufferData)(unsigned, ptrdiff_t, const void *, unsigned);
    void (VGL_CALL *VertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *);
    void (VGL_CALL *EnableVertexAttribArray)(unsigned);
} VGL;

extern VGL vgl;
int vgl_load(void); /* call after the GL context is current; 0 on failure */
#endif
