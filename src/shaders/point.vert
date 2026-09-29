#version 460 core

layout (location = 0) in vec3 in_position;

uniform mat4 view;
uniform mat4 projection;

out vec3 color;

void main()
{
    gl_Position = projection * view * vec4(in_position, 1.0);
    const float world_size = 0.001;
    const float view_height = 720;
    gl_PointSize = clamp(world_size * projection[1][1] * view_height / (2.0 * gl_Position.w), 1.0, 100.0);
    
    color = vec3(in_position.z * 0.01);
}