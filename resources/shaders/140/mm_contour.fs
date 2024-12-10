#version 140

const float EPSILON = 0.0001;

out vec4 frag_color;
void main()
{
    frag_color = vec4(1.0, 1.0, 1.0, 1.0);
    // Values inside depth buffer for fragments of the contour of a selected area are offset
    // by small epsilon to solve z-fighting between painted triangles and contour lines.
    gl_FragDepth = gl_FragCoord.z - EPSILON;
}
