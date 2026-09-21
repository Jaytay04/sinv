#ifdef MODULE_ESP

#include <cstdint>
#include <cmath>

namespace hypersinv::esp::arc_raiders {

/*
 * World-to-screen projection.
 * 
 * Transforms a 3D world position to 2D screen coordinates using
 * the provided view-projection matrix and viewport dimensions.
 * 
 * The matrix is expected to be row-major 4x4, and the input vector
 * is treated as a column vector [x y z 1].
 * 
 * Returns true if the point is in front of the camera and within
 * the viewport, false otherwise.
 */
bool WorldToScreen(float world_x, float world_y, float world_z,
                   const float* view_projection,
                   float viewport_width, float viewport_height,
                   float* out_screen_x, float* out_screen_y) {
    /* Multiply world position by view-projection matrix */
    float clip_x = world_x * view_projection[0] + 
                   world_y * view_projection[4] + 
                   world_z * view_projection[8] + 
                   view_projection[12];
                   
    float clip_y = world_x * view_projection[1] + 
                   world_y * view_projection[5] + 
                   world_z * view_projection[9] + 
                   view_projection[13];
                   
    float clip_z = world_x * view_projection[2] + 
                   world_y * view_projection[6] + 
                   world_z * view_projection[10] + 
                   view_projection[14];
                   
    float clip_w = world_x * view_projection[3] + 
                   world_y * view_projection[7] + 
                   world_z * view_projection[11] + 
                   view_projection[15];
    
    /* Check if point is behind camera */
    if (clip_w <= 0.0001f) {
        return false;
    }
    
    /* Normalize to NDC space */
    float ndc_x = clip_x / clip_w;
    float ndc_y = clip_y / clip_w;
    
    /* Check if point is within viewport bounds */
    if (ndc_x < -1.0f || ndc_x > 1.0f || ndc_y < -1.0f || ndc_y > 1.0f) {
        return false;
    }
    
    /* Convert NDC to screen coordinates */
    *out_screen_x = (ndc_x * 0.5f + 0.5f) * viewport_width;
    *out_screen_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * viewport_height;
    
    return true;
}

/*
 * Project an entity's bounding box to screen space.
 * 
 * Takes the entity's world position and dimensions, projects all
 * eight corners of the bounding box, and computes the minimal
 * screen-space rectangle that contains all visible corners.
 * 
 * Returns true if at least one corner is visible, false otherwise.
 */
bool ProjectBoundingBox(float world_x, float world_y, float world_z,
                       float width, float height, float depth,
                       const float* view_projection,
                       float viewport_width, float viewport_height,
                       float* out_screen_x, float* out_screen_y,
                       float* out_screen_width, float* out_screen_height) {
    float corners[8][3] = {
        {world_x - width/2, world_y - height/2, world_z - depth/2},
        {world_x + width/2, world_y - height/2, world_z - depth/2},
        {world_x - width/2, world_y + height/2, world_z - depth/2},
        {world_x + width/2, world_y + height/2, world_z - depth/2},
        {world_x - width/2, world_y - height/2, world_z + depth/2},
        {world_x + width/2, world_y - height/2, world_z + depth/2},
        {world_x - width/2, world_y + height/2, world_z + depth/2},
        {world_x + width/2, world_y + height/2, world_z + depth/2}
    };
    
    float min_x = viewport_width, max_x = 0.0f;
    float min_y = viewport_height, max_y = 0.0f;
    bool any_visible = false;
    
    for (int i = 0; i < 8; ++i) {
        float screen_x, screen_y;
        if (WorldToScreen(corners[i][0], corners[i][1], corners[i][2],
                         view_projection, viewport_width, viewport_height,
                         &screen_x, &screen_y)) {
            any_visible = true;
            if (screen_x < min_x) min_x = screen_x;
            if (screen_x > max_x) max_x = screen_x;
            if (screen_y < min_y) min_y = screen_y;
            if (screen_y > max_y) max_y = screen_y;
        }
    }
    
    if (!any_visible) {
        return false;
    }
    
    *out_screen_x = min_x;
    *out_screen_y = min_y;
    *out_screen_width = max_x - min_x;
    *out_screen_height = max_y - min_y;
    
    return true;
}

} // namespace hypersinv::esp::arc_raiders

#endif /* MODULE_ESP */
