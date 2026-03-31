// Guo forcing term helper
// –ß‚è’l: f_force (scalar) ‚ğ f_i ‚É‘«‚·
float GuoForceScalar(float wi, float3 ei, float3 u, float3 F, float cs2, float tau)
{
    // ˆÀ‘S‚È’è”
    float inv_cs2 = 1.0f / cs2;
    float inv_cs4 = inv_cs2 * inv_cs2;
    float oneMinusHalfInvTau = (1.0f - 0.5f / tau);

    float ei_dot_u = dot(ei, u);
    float ei_dot_F = dot(ei, F);
    float term = (dot(ei - u, F) * inv_cs2) + (ei_dot_u * ei_dot_F) * inv_cs4;
    float Fi = wi * term * oneMinusHalfInvTau;
    return Fi;
}