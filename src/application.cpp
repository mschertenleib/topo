#include "application.hpp"
#include "unique_handle.hpp"

#define GLFW_INCLUDE_GLCOREARB
#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

#define ENUMERATE_GL_FUNCTIONS(F)                                              \
    F(PFNGLENABLEPROC, glEnable);                                              \
    F(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback);                  \
    F(PFNGLCREATESHADERPROC, glCreateShader);                                  \
    F(PFNGLDELETESHADERPROC, glDeleteShader);                                  \
    F(PFNGLSHADERSOURCEPROC, glShaderSource);                                  \
    F(PFNGLCOMPILESHADERPROC, glCompileShader);                                \
    F(PFNGLGETSHADERIVPROC, glGetShaderiv);                                    \
    F(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);                          \
    F(PFNGLCREATEPROGRAMPROC, glCreateProgram);                                \
    F(PFNGLDELETEPROGRAMPROC, glDeleteProgram);                                \
    F(PFNGLATTACHSHADERPROC, glAttachShader);                                  \
    F(PFNGLLINKPROGRAMPROC, glLinkProgram);                                    \
    F(PFNGLGETPROGRAMIVPROC, glGetProgramiv);                                  \
    F(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog);                        \
    F(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);                      \
    F(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays);                            \
    F(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays);                      \
    F(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray);                            \
    F(PFNGLGENBUFFERSPROC, glGenBuffers);                                      \
    F(PFNGLDELETEBUFFERSPROC, glDeleteBuffers);                                \
    F(PFNGLBINDBUFFERPROC, glBindBuffer);                                      \
    F(PFNGLBUFFERDATAPROC, glBufferData);                                      \
    F(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer);                    \
    F(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);            \
    F(PFNGLVIEWPORTPROC, glViewport);                                          \
    F(PFNGLCLEARCOLORPROC, glClearColor);                                      \
    F(PFNGLCLEARPROC, glClear);                                                \
    F(PFNGLUSEPROGRAMPROC, glUseProgram);                                      \
    F(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv);                          \
    F(PFNGLDRAWARRAYSPROC, glDrawArrays);                                      \
    F(PFNGLBLENDFUNCPROC, glBlendFunc);                                        \
    F(PFNGLDEPTHMASKPROC, glDepthMask);

// clang-format off
#define DECLARE_GL_FUNCTION(type, name) type name {nullptr}
// clang-format on

ENUMERATE_GL_FUNCTIONS(DECLARE_GL_FUNCTION)

struct GL_deleter
{
    void (*destroy)(GLuint);
    void operator()(GLuint handle)
    {
        destroy(handle);
    }
};

struct GL_array_deleter
{
    void (*destroy)(GLsizei, const GLuint *);
    void operator()(GLuint handle)
    {
        destroy(1, &handle);
    }
};

struct vec3
{
    float x;
    float y;
    float z;
};

struct mat4
{
    float m[16];
};

struct Camera
{
    vec3 position;
    float yaw;
    float pitch;
    float sensitivity;
    float speed;
    double last_mouse_x;
    double last_mouse_y;
};

void glfw_error_callback(int error, const char *description)
{
    std::cerr << "GLFW error " << error << ": " << description << '\n';
}

void load_gl_functions()
{
#define LOAD_GL_FUNCTION(type, name)                                           \
    name = reinterpret_cast<type>(glfwGetProcAddress(#name));                  \
    assert(name != nullptr)

    ENUMERATE_GL_FUNCTIONS(LOAD_GL_FUNCTION)

#undef LOAD_GL_FUNCTION
}

void APIENTRY gl_debug_callback([[maybe_unused]] GLenum source,
                                GLenum type,
                                [[maybe_unused]] GLuint id,
                                GLenum severity,
                                [[maybe_unused]] GLsizei length,
                                const GLchar *message,
                                [[maybe_unused]] const void *user_param)
{
    if (type == GL_DEBUG_TYPE_OTHER ||
        severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    {
        return;
    }
    std::cerr << message << '\n';
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error(
            std::format("File \"{}\" does not exist", path.string()));
    }

    const auto file_size = std::filesystem::file_size(path);

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error(
            std::format("Failed to open file \"{}\"", path.string()));
    }

    std::string result;
    result.resize_and_overwrite(
        file_size,
        [&file](char *buf, std::size_t buf_size)
        {
            file.read(buf, static_cast<std::streamsize>(buf_size));
            return file.gcount();
        });

    return result;
}

[[nodiscard]] std::vector<vec3> load_xyz(const std::filesystem::path &path)
{
    const auto data = read_file(path);

    const char *p {data.data()};
    const char *const end {p + data.size()};

    const auto next_line = [end] [[nodiscard]] (const char *ptr) noexcept
    {
        while (ptr < end && *ptr != '\n')
            ++ptr;
        if (ptr < end)
            ++ptr;
        return ptr;
    };

    // Skip header
    p = next_line(p);

    std::vector<vec3> points;

    const auto line_length = next_line(p) - p;
    const auto data_length = end - p;
    const auto num_lines = data_length / line_length;
    points.reserve(static_cast<std::size_t>(num_lines));

    while (p < end)
    {
        float x {};
        const auto [px, ecx] = std::from_chars(p, end, x);
        p = px + 1; // Skip space

        float y {};
        const auto [py, ecy] = std::from_chars(p, end, y);
        p = py + 1; // Skip space

        float z {};
        const auto [pz, ecz] = std::from_chars(p, end, z);
        p = pz;

        points.emplace_back(x, y, z);

        if (p < end && *p == '\r')
            ++p;
        if (p < end && *p == '\n')
            ++p;
    }

    return points;
}

[[nodiscard]] auto create_shader(GLenum type, const char *source)
{
    Unique_handle shader(glCreateShader(type), GL_deleter {glDeleteShader});

    glShaderSource(shader.get(), 1, &source, nullptr);
    glCompileShader(shader.get());

    int success {};
    glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &success);
    if (!success)
    {
        int buf_length {};
        glGetShaderiv(shader.get(), GL_INFO_LOG_LENGTH, &buf_length);
        std::string message(static_cast<std::size_t>(buf_length), '\0');
        glGetShaderInfoLog(shader.get(), buf_length, nullptr, message.data());
        throw std::runtime_error(
            std::format("Shader compilation failed:\n{}\n", message));
    }

    return shader;
}

[[nodiscard]] auto create_program(GLuint vertex_shader, GLuint fragment_shader)
{
    Unique_handle program(glCreateProgram(), GL_deleter {glDeleteProgram});

    glAttachShader(program.get(), vertex_shader);
    glAttachShader(program.get(), fragment_shader);
    glLinkProgram(program.get());

    int success {};
    glGetProgramiv(program.get(), GL_LINK_STATUS, &success);
    if (!success)
    {
        int buf_length {};
        glGetProgramiv(program.get(), GL_INFO_LOG_LENGTH, &buf_length);
        std::string message(static_cast<std::size_t>(buf_length), '\0');
        glGetProgramInfoLog(program.get(), buf_length, nullptr, message.data());
        throw std::runtime_error(
            std::format("Program linking failed:\n{}\n", message));
    }

    return program;
}

[[nodiscard]] auto
create_program(const std::filesystem::path &vertex_shader_path,
               const std::filesystem::path &fragment_shader_path)
{
    const auto vertex_shader_code = read_file(vertex_shader_path);
    const auto vertex_shader =
        create_shader(GL_VERTEX_SHADER, vertex_shader_code.c_str());

    const auto fragment_shader_code = read_file(fragment_shader_path);
    const auto fragment_shader =
        create_shader(GL_FRAGMENT_SHADER, fragment_shader_code.c_str());

    return create_program(vertex_shader.get(), fragment_shader.get());
}

[[nodiscard]] auto create_vertex_buffer(std::span<const vec3> vertices)
{
    GLuint vao_gl {};
    glGenVertexArrays(1, &vao_gl);
    Unique_handle vao(vao_gl, GL_array_deleter {glDeleteVertexArrays});
    glBindVertexArray(vao.get());

    GLuint vbo_gl {};
    glGenBuffers(1, &vbo_gl);
    Unique_handle vbo(vbo_gl, GL_array_deleter {glDeleteBuffers});
    glBindBuffer(GL_ARRAY_BUFFER, vbo.get());
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizei>(vertices.size_bytes()),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), 0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    return std::tuple {std::move(vao), std::move(vbo)};
}

[[nodiscard]] constexpr float dot(const vec3 &a, const vec3 &b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr vec3 cross(const vec3 &a, const vec3 &b) noexcept
{
    return {
        a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] float norm(const vec3 &v) noexcept
{
    return std::sqrt(dot(v, v));
}

[[nodiscard]] vec3 normalize(const vec3 &v) noexcept
{
    const auto length = norm(v);
    if (length == 0.0f)
    {
        return {0.0f, 0.0f, 0.0f};
    }
    const auto inv_length = 1.0f / length;
    return {v.x * inv_length, v.y * inv_length, v.z * inv_length};
}

[[nodiscard]] vec3 camera_forward(float yaw, float pitch) noexcept
{
    const auto cos_pitch = std::cos(pitch);
    return normalize({cos_pitch * std::cos(yaw),
                      cos_pitch * std::sin(yaw),
                      std::sin(pitch)});
}

[[nodiscard]] mat4 make_view_matrix(const vec3 &position,
                                    const vec3 &forward) noexcept
{
    assert(std::abs(norm(forward) - 1.0f) < 1e-5f);

    constexpr vec3 world_up {0.0f, 0.0f, 1.0f};
    const auto right = normalize(cross(forward, world_up));
    const auto up = cross(right, forward);

    mat4 result {};

    result.m[0] = right.x;
    result.m[1] = up.x;
    result.m[2] = -forward.x;
    result.m[3] = 0.0f;

    result.m[4] = right.y;
    result.m[5] = up.y;
    result.m[6] = -forward.y;
    result.m[7] = 0.0f;

    result.m[8] = right.z;
    result.m[9] = up.z;
    result.m[10] = -forward.z;
    result.m[11] = 0.0f;

    result.m[12] = -dot(right, position);
    result.m[13] = -dot(up, position);
    result.m[14] = dot(forward, position);
    result.m[15] = 1.0f;

    return result;
}

[[nodiscard]] mat4 make_perspective_matrix(float vertical_fov,
                                           float aspect_ratio,
                                           float near_plane) noexcept
{
    const auto f = 1.0f / std::tan(vertical_fov * 0.5f);

    mat4 result {};

    result.m[0] = f / aspect_ratio;
    result.m[1] = 0.0f;
    result.m[2] = 0.0f;
    result.m[3] = 0.0f;

    result.m[4] = 0.0f;
    result.m[5] = f;
    result.m[6] = 0.0f;
    result.m[7] = 0.0f;

    result.m[8] = 0.0f;
    result.m[9] = 0.0f;
    result.m[10] = -1.0f;
    result.m[11] = -1.0f;

    result.m[12] = 0.0f;
    result.m[13] = 0.0f;
    result.m[14] = -2.0f * near_plane;
    result.m[15] = 0.0f;

    return result;
}

void update_camera(GLFWwindow *window, float delta_t, Camera &camera)
{
    double mouse_x {};
    double mouse_y {};
    glfwGetCursorPos(window, &mouse_x, &mouse_y);

    const auto dx = static_cast<float>(mouse_x - camera.last_mouse_x);
    const auto dy = static_cast<float>(mouse_y - camera.last_mouse_y);

    camera.last_mouse_x = mouse_x;
    camera.last_mouse_y = mouse_y;

    camera.yaw -= dx * camera.sensitivity;
    camera.pitch -= dy * camera.sensitivity;
    camera.pitch = std::clamp(camera.pitch,
                              -0.49f * std::numbers::pi_v<float>,
                              0.49f * std::numbers::pi_v<float>);

    const auto forward = camera_forward(camera.yaw, camera.pitch);
    const auto forward_xy = normalize(vec3 {forward.x, forward.y, 0.0f});
    const vec3 right {forward_xy.y, -forward_xy.x, 0.0f};

    const auto delta = camera.speed * delta_t;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    {
        camera.position.x += forward_xy.x * delta;
        camera.position.y += forward_xy.y * delta;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {
        camera.position.x -= forward_xy.x * delta;
        camera.position.y -= forward_xy.y * delta;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    {
        camera.position.x += right.x * delta;
        camera.position.y += right.y * delta;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    {
        camera.position.x -= right.x * delta;
        camera.position.y -= right.y * delta;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
    {
        camera.position.z += delta;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    {
        camera.position.z -= delta;
    }
}

} // namespace

void run_application()
{
    glfwSetErrorCallback(&glfw_error_callback);

    const Unique_handle glfw_context(glfwInit(), [] { glfwTerminate(); });
    if (!glfw_context.has_value())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_CONTEXT_DEBUG, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    const Unique_handle window(
        glfwCreateWindow(1280, 720, "Geo", nullptr, nullptr),
        [](GLFWwindow *w) { glfwDestroyWindow(w); });
    if (!window.has_value())
    {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(1);

    load_gl_functions();

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&gl_debug_callback, nullptr);

    const auto program =
        create_program("shaders/shader.vert", "shaders/shader.frag");
    const auto loc_view = glGetUniformLocation(program.get(), "view");
    const auto loc_projection =
        glGetUniformLocation(program.get(), "projection");

    glEnable(GL_PROGRAM_POINT_SIZE);
    // glEnable(GL_BLEND);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

#if 0
    auto vertices =
        load_xyz("../../SWISSALTI3D_0.5_XYZ_CHLV95_LN02_2538_1152.xyz");
#else
    auto vertices = load_xyz(
        "../../swissSURFACE3D_Raster_0.5_xyz_CHLV95_LN02_2538_1152.xyz");
#endif

    float min_x {std::numeric_limits<float>::infinity()};
    float min_y {std::numeric_limits<float>::infinity()};
    float min_z {std::numeric_limits<float>::infinity()};
    float max_x {-std::numeric_limits<float>::infinity()};
    for (const auto &vertex : vertices)
    {
        if (vertex.x < min_x)
            min_x = vertex.x;
        if (vertex.y < min_y)
            min_y = vertex.y;
        if (vertex.z < min_z)
            min_z = vertex.z;
        if (vertex.x > max_x)
            max_x = vertex.x;
    }

    const auto scale = 1.0f / (max_x - min_x);
    for (auto &vertex : vertices)
    {
        vertex.x = (vertex.x - min_x) * scale;
        vertex.y = (vertex.y - min_y) * scale;
        vertex.z = (vertex.z - min_z) * scale;
    }

    const auto [vao, vbo] = create_vertex_buffer(vertices);
    glBindVertexArray(vao.get());

    if (glfwRawMouseMotionSupported())
    {
        glfwSetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    bool camera_active {false};
    double mouse_x {};
    double mouse_y {};
    glfwGetCursorPos(window.get(), &mouse_x, &mouse_y);
    Camera camera {.position = {-1.0f, 0.0f, 0.5f},
                   .yaw = 0.0f,
                   .pitch = 0.0f,
                   .sensitivity = 0.005f,
                   .speed = 0.5f,
                   .last_mouse_x = mouse_x,
                   .last_mouse_y = mouse_y};

    int num_frames {0};
    double last_time {glfwGetTime()};
    double last_frame_time {glfwGetTime()};
    bool e_pressed {false};
    bool q_pressed {false};

    while (!glfwWindowShouldClose(window.get()))
    {
        glfwPollEvents();

        if (glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS &&
            camera_active)
        {
            camera_active = false;
            glfwSetInputMode(window.get(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        else if (glfwGetMouseButton(window.get(), GLFW_MOUSE_BUTTON_LEFT) ==
                     GLFW_PRESS &&
                 !camera_active)
        {
            camera_active = true;
            glfwSetInputMode(window.get(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }

        if (glfwGetKey(window.get(), GLFW_KEY_E) == GLFW_PRESS)
        {
            if (!e_pressed)
            {
                e_pressed = true;
                camera.speed *= 2.0f;
            }
        }
        else
        {
            e_pressed = false;
        }

        if (glfwGetKey(window.get(), GLFW_KEY_Q) == GLFW_PRESS)
        {
            if (!q_pressed)
            {
                q_pressed = true;
                camera.speed *= 0.5f;
            }
        }
        else
        {
            q_pressed = false;
        }

        const auto frame_time = glfwGetTime();
        const auto delta_t = static_cast<float>(frame_time - last_frame_time);
        if (camera_active)
        {
            update_camera(window.get(), delta_t, camera);
        }
        last_frame_time = frame_time;

        int width {};
        int height {};
        glfwGetFramebufferSize(window.get(), &width, &height);
        glViewport(0, 0, width, height);

        glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program.get());

        const auto view_matrix = make_view_matrix(
            camera.position, camera_forward(camera.yaw, camera.pitch));
        const auto projection_matrix = make_perspective_matrix(
            90.0f / 180.0f * std::numbers::pi_v<float>,
            static_cast<float>(width) / static_cast<float>(height),
            0.001f);

        glUniformMatrix4fv(loc_view, 1, GL_FALSE, view_matrix.m);
        glUniformMatrix4fv(loc_projection, 1, GL_FALSE, projection_matrix.m);

        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(vertices.size()));

        glfwSwapBuffers(window.get());

        ++num_frames;
        const double current_time {glfwGetTime()};
        if (const auto elapsed = current_time - last_time; elapsed >= 1.0)
        {
            std::cout << std::format("{:7.2f} fps\r",
                                     static_cast<double>(num_frames) / elapsed)
                      << std::flush;
            num_frames = 0;
            last_time = current_time;
        }
    }
}
