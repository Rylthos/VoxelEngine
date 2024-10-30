#version 450

#extension GL_EXT_buffer_reference : enable

layout (buffer_reference, std430) readonly buffer VertexBuffer
{
    vec4 vertices[];
};

layout (push_constant) uniform constants
{
    VertexBuffer vertexBuffer;
} PushConstants;

void main()
{
    vec4 v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

    gl_Position = v;
}
