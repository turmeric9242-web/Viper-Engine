/* TEST STUB ONLY - lets the engine's window/GL/engine code compile and run headless without real GLFW. */
#ifndef STUB_GLFW3_H
#define STUB_GLFW3_H
typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWglproc)(void);
typedef void (*GLFWkeyfun)(GLFWwindow *, int, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow *, double, double);
typedef void (*GLFWscrollfun)(GLFWwindow *, double, double);
#define GLFW_TRUE 1
#define GLFW_FALSE 0
#define GLFW_PRESS 1
#define GLFW_RELEASE 0
#define GLFW_CONTEXT_VERSION_MAJOR 1
#define GLFW_CONTEXT_VERSION_MINOR 2
#define GLFW_OPENGL_PROFILE 3
#define GLFW_OPENGL_CORE_PROFILE 4
#define GLFW_SAMPLES 5
#define GLFW_CURSOR 6
#define GLFW_CURSOR_NORMAL 7
#define GLFW_CURSOR_DISABLED 8
#define GLFW_RAW_MOUSE_MOTION 9
#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_DEPTH_TEST 0xB71
#define GL_TRIANGLES 4
#define GL_FLOAT 0x1406
#define GL_BLEND 0xBE2
#define GL_SRC_ALPHA 0x302
#define GL_ONE_MINUS_SRC_ALPHA 0x303
#define GL_TEXTURE_2D 0xDE1
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
int glfwInit(void); void glfwTerminate(void);
void glfwWindowHint(int, int);
GLFWwindow *glfwCreateWindow(int, int, const char *, void *, void *);
void glfwDestroyWindow(GLFWwindow *);
void glfwMakeContextCurrent(GLFWwindow *); void glfwSwapInterval(int); void glfwSwapBuffers(GLFWwindow *);
void glfwSetWindowUserPointer(GLFWwindow *, void *); void *glfwGetWindowUserPointer(GLFWwindow *);
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *, GLFWkeyfun);
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *, GLFWcursorposfun);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow *, GLFWscrollfun);
void glfwGetWindowSize(GLFWwindow *, int *, int *);
void glfwGetCursorPos(GLFWwindow *, double *, double *);
void glfwPollEvents(void); int glfwWindowShouldClose(GLFWwindow *); void glfwSetWindowShouldClose(GLFWwindow *, int);
void glfwGetFramebufferSize(GLFWwindow *, int *, int *); void glfwSetWindowTitle(GLFWwindow *, const char *);
double glfwGetTime(void); GLFWglproc glfwGetProcAddress(const char *);
void glfwSetInputMode(GLFWwindow *, int, int); int glfwRawMouseMotionSupported(void);
int glfwGetMouseButton(GLFWwindow *, int);
void glClear(unsigned); void glClearColor(float, float, float, float); void glEnable(unsigned);
void glViewport(int, int, int, int); void glDrawArrays(unsigned, int, int);
void glDisable(unsigned); void glBlendFunc(unsigned, unsigned); void glBindTexture(unsigned, unsigned);
void glGenTextures(int, unsigned *); void glTexParameteri(unsigned, unsigned, int);
void glTexImage2D(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
#endif
