#version 140
uniform sampler2D u_sampler;
uniform mat3 u_convolution_matrix;
uniform vec3 u_viewport_size_alpha;
uniform vec3 u_picking_color;

in vec2 tex_coords;

out vec4 frag_color;

vec4 sample(float offsetX, float offsetY)
{
    return texture(u_sampler, vec2(tex_coords.x + offsetX, tex_coords.y + offsetY));
}
void main()
{
    vec4 pixels[9];
    float deltaWidth = 1.0 / u_viewport_size_alpha.x;
    float deltaHeight = 1.0 / u_viewport_size_alpha.y;
    float alpha = u_viewport_size_alpha.z;
    float effect_width = 2.0;
    deltaWidth = deltaWidth * effect_width;
    deltaHeight = deltaHeight * effect_width;

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
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; ++j)
        {
            accumulator += sign(pixels[3 * i + j].a) * vec4(u_picking_color, 1.0) *  u_convolution_matrix[i][j];
        }
    }

    if ((abs(accumulator.a) - alpha * pixels[4].a) * 1e6 > 1.0) {
        accumulator = vec4(u_picking_color, abs(accumulator.a));
    }
    frag_color = accumulator;
}