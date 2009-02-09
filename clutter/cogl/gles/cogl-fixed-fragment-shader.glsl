/*** cogl_fixed_fragment_shader_header_start ***/

/* There is no default precision for floats in fragment shaders in
   GLES 2 so we need to define one */
precision lowp float;

/* Inputs from the vertex shader */
varying mediump vec2       tex_coord;

/* Texturing options */
uniform lowp sampler2D  texture_unit;

/* Alpha test options */
uniform lowp float      alpha_test_ref;

/*** cogl_fixed_fragment_shader_header_color ***/
varying lowp vec4       frag_color;
/*** cogl_fixed_fragment_shader_header_fog ***/

/* Fogging options */
uniform lowp vec4       fog_color;
/* Fogging Inputs from the vertex shader */
varying lowp float      fog_amount;

/*** cogl_fixed_fragment_shader_start ***/

void
main (void)
{
  /*** cogl_fixed_fragment_shader_texture_alpha_only ***/

  /* If the texture only has an alpha channel (eg, with the textures
     from the pango renderer) then the RGB components will be
     black. We want to use the RGB from the current color in that
     case */
  gl_FragColor = frag_color;
  gl_FragColor.a *= texture2D (texture_unit, tex_coord).a;

  /*** cogl_fixed_fragment_shader_texture_color ***/

  /* This pointless extra variable is needed to work around an
     apparent bug in the PowerVR drivers. Without it the alpha
     blending seems to stop working */
  lowp vec4 frag_color_copy = frag_color;
  gl_FragColor = frag_color_copy * texture2D (texture_unit, tex_coord);
  
  /*** cogl_fixed_fragment_shader_texture ***/
  gl_FragColor = texture2D (texture_unit, tex_coord);

  /*** cogl_fixed_fragment_shader_solid_color ***/
  gl_FragColor = frag_color;

  /*** cogl_fixed_fragment_shader_fog ***/

  /* Mix the calculated color with the fog color */
  gl_FragColor.rgb = mix (fog_color.rgb, gl_FragColor.rgb, fog_amount);

  /* Alpha testing */

  /*** cogl_fixed_fragment_shader_alpha_never ***/
  discard;
  /*** cogl_fixed_fragment_shader_alpha_less ***/
  if (gl_FragColor.a >= alpha_test_ref)
    discard;
  /*** cogl_fixed_fragment_shader_alpha_equal ***/
  if (gl_FragColor.a != alpha_test_ref)
    discard;
  /*** cogl_fixed_fragment_shader_alpha_lequal ***/
  if (gl_FragColor.a > alpha_test_ref)
    discard;
  /*** cogl_fixed_fragment_shader_alpha_greater ***/
  if (gl_FragColor.a <= alpha_test_ref)
    discard;
  /*** cogl_fixed_fragment_shader_alpha_notequal ***/
  if (gl_FragColor.a == alpha_test_ref)
    discard;
  /*** cogl_fixed_fragment_shader_alpha_gequal ***/
  if (gl_FragColor.a < alpha_test_ref)
    discard;

  /*** cogl_fixed_fragment_shader_end ***/
}
