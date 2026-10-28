/* TEST STUB ONLY - lets the engine's window/GL/engine code compile and run headless without real GLFW. */
#ifndef STUB_GLFW3_H
#define STUB_GLFW3_H
typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWglproc)(void);
typedef void (*GLFWkeyfun)(GLFWwindow *, int, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow *, double, double);
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
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_DEPTH_TEST 0xB71
#define GL_TRIANGLES 4
#define GL_FLOAT 0x1406
int glfwInit(void); void glfwTerminate(void);
void glfwWindowHint(int, int);
GLFWwindow *glfwCreateWindow(int, int, const char *, void *, void *);
void glfwDestroyWindow(GLFWwindow *);
void glfwMakeContextCurrent(GLFWwindow *); void glfwSwapInterval(int); void glfwSwapBuffers(GLFWwindow *);
void glfwSetWindowUserPointer(GLFWwindow *, void *); void *glfwGetWindowUserPointer(GLFWwindow *);
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *, GLFWkeyfun);
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *, GLFWcursorposfun);
void glfwGetCursorPos(GLFWwindow *, double *, double *);
void glfwPollEvents(void); int glfwWindowShouldClose(GLFWwindow *); void glfwSetWindowShouldClose(GLFWwindow *, int);
void glfwGetFramebufferSize(GLFWwindow *, int *, int *); void glfwSetWindowTitle(GLFWwindow *, const char *);
double glfwGetTime(void); GLFWglproc glfwGetProcAddress(const char *);
void glfwSetInputMode(GLFWwindow *, int, int); int glfwRawMouseMotionSupported(void);
int glfwGetMouseButton(GLFWwindow *, int);
void glClear(unsigned); void glClearColor(float, float, float, float); void glEnable(unsigned);
void glViewport(int, int, int, int); void glDrawArrays(unsigned, int, int);
#endif
