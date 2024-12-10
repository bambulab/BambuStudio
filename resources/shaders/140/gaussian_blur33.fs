#version 140
uniform sampler2D u_sampler;
uniform mat3 u_convolution_matrix;
uniform vec2 u_viewport_size;

in vec2 tex_coords;

out vec4 frag_color;

vec4 sample(float offsetX, float offsetY)
{
    return texture(u_sampler, vec2(tex_coords.x + offsetX, tex_coords.y + offsetY));
}
void main()
{
    vec4 pixels[9];
    float deltaWidth = 1.0 / u_viewport_size.x;
    float deltaHeight = 1.0 / u_viewport_size.y;
    pixels[0] = sample(-deltaWidth, deltaHeight );
    pixels[1] = sample(0.0, deltaHeight );
    pixels[2] = sample(deltaWidth, deltaHeight );
    pixels[3] = sample(-deltaWidth, 0.0);
    pixels[4] = sample(0.0, 0.0);
    pixels[5] = sample(deltaWidth, 0.0);
    pixels[6] = sample(-deltaWidth, -deltaHeight);
    pixels[7] = sample(0.0, -deltaHeight);
    pixels[8] = sample(deltaWidth, -deltaHeight);
    vec4 accumulator = vec4(0.0);
    for (int i = 0; i < 9; ++i)
    {
        accumulator += pixels[i] * u_convolution_matrix[i / 3][i % 3];
    }
    frag_color = accumulator;
}