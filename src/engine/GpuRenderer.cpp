#include "engine/GpuRenderer.hpp"
#include "engine/GpuGeometryRules.hpp"
#include "engine/TouchOverlay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <iterator>
#include <vector>

namespace originalengine {
namespace {

struct GpuVertex {
    float position[3];
    float normal[3];
    float color[3];
    float uv[2];
    float hasTexCoord;
};

using Matrix4 = std::array<float, 16>;

struct ObjectConstants {
    Matrix4 mvp{};
    Matrix4 model{};
    Matrix4 normalMatrix{};
    float cameraPosition[4]{};
    float materialFlags[4]{};
    float pointLightPositionRange[4]{};
    float pointLightColorIntensity[4]{};
    Matrix4 shadowMvp{};
    float depthFlags[4]{};
};

struct CompositeConstants {
    Matrix4 inverseViewProjection{};
    Matrix4 viewProjection{};
    float cameraPosition[4]{};
    float depthFlags[4]{};
};

constexpr char vertexSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 color [[attribute(2)]];
    float2 uv [[attribute(3)]];
    float hasTexCoord [[attribute(4)]];
};

struct VertexOutput {
    float4 position [[position]];
    float3 normal;
    float3 color;
    float2 uv;
    float hasTexCoord;
    float3 viewDirection;
    float3 worldPosition;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
    float isFloor;
    float isEmissive;
    float materialMetallic;
    float materialRoughness;
    float4 lightSpacePosition;
    float shadowDepthPacked;
};

struct CameraConstants {
    float4x4 mvp;
    float4x4 model;
    float4x4 normalMatrix;
    float4 cameraPosition;
    float4 materialFlags;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
    float4x4 shadowMvp;
    float4 depthFlags;
};

vertex VertexOutput main_vertex(
    VertexInput input [[stage_in]],
    constant CameraConstants& camera [[buffer(0)]])
{
    VertexOutput output;
    const float4 worldPosition = camera.model * float4(input.position, 1.0);
    output.position = camera.mvp * float4(input.position, 1.0);
    output.normal = normalize((camera.normalMatrix * float4(input.normal, 0.0)).xyz);
    output.color = input.color;
    output.uv = input.uv;
    output.hasTexCoord = input.hasTexCoord;
    output.viewDirection = normalize(camera.cameraPosition.xyz - worldPosition.xyz);
    output.worldPosition = worldPosition.xyz;
    output.pointLightPositionRange = camera.pointLightPositionRange;
    output.pointLightColorIntensity = camera.pointLightColorIntensity;
    output.isFloor = camera.materialFlags.x;
    output.isEmissive = camera.materialFlags.y;
    output.materialMetallic = camera.materialFlags.z;
    output.materialRoughness = camera.materialFlags.w;
    output.lightSpacePosition = camera.shadowMvp * worldPosition;
    output.shadowDepthPacked = camera.depthFlags.x;
    return output;
}
)metal";

constexpr char fragmentSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexOutput {
    float4 position [[position]];
    float3 normal;
    float3 color;
    float2 uv;
    float hasTexCoord;
    float3 viewDirection;
    float3 worldPosition;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
    float isFloor;
    float isEmissive;
    float materialMetallic;
    float materialRoughness;
    float4 lightSpacePosition;
    float shadowDepthPacked;
};

struct CameraConstants {
    float4x4 mvp;
    float4x4 model;
    float4x4 normalMatrix;
    float4 cameraPosition;
    float4 materialFlags;
    float4 pointLightPositionRange;
    float4 pointLightColorIntensity;
};

fragment float4 main_fragment(
    VertexOutput input [[stage_in]],
    texture2d<float> baseColorTexture [[texture(0)]],
    sampler baseColorSampler [[sampler(0)]],
    texture2d<float> shadowMap [[texture(1)]],
    sampler shadowSampler [[sampler(1)]])
{
    const float3 sampledColor = input.hasTexCoord < 0.5
        ? input.color
        : baseColorTexture.sample(baseColorSampler, input.uv).rgb * input.color;
    const float3 normal = normalize(input.normal);
    const bool floorSurface = input.isFloor > 0.5;
    const float3 cyanTint = floorSurface
        ? float3(0.025, 0.20, 0.32)
        : float3(0.12, 0.78, 1.0);
    const float3 albedo = sampledColor * cyanTint;
    const float metallic = clamp(input.materialMetallic, 0.0, 1.0);
    const float roughness = clamp(input.materialRoughness, 0.045, 1.0);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float3 viewDirection = normalize(input.viewDirection);
    const float3 f0 = mix(float3(0.04), albedo, metallic);

    // Cook-Torrance BRDF with GGX normal distribution, Smith-Schlick
    // geometry, and Schlick Fresnel. Kept in one forward pass for mobile GPUs.
    const float3 directionalLight = normalize(float3(-0.35, 0.8, -0.45));
    const float3 directionalHalf = normalize(viewDirection + directionalLight);
    const float nDotV = max(dot(normal, viewDirection), 0.001);
    const float nDotL = max(dot(normal, directionalLight), 0.0);
    const float nDotH = max(dot(normal, directionalHalf), 0.0);
    const float vDotH = max(dot(viewDirection, directionalHalf), 0.0);
    const float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    const float distribution = alphaSquared /
        max(3.14159265 * denominator * denominator, 0.001);
    const float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    const float geometryV = nDotV / (nDotV * (1.0 - k) + k);
    const float geometryL = nDotL / (nDotL * (1.0 - k) + k);
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
    const float3 specular = distribution * geometryV * geometryL * fresnel /
        max(4.0 * nDotV * max(nDotL, 0.001), 0.001);
    const float3 diffuseWeight = (1.0 - fresnel) * (1.0 - metallic);
    const float3 directionalBrdf =
        diffuseWeight * albedo / 3.14159265 + specular;
    float shadowVisibility = 1.0;
    if (input.lightSpacePosition.w > 0.0001) {
        const float3 ndc = input.lightSpacePosition.xyz / input.lightSpacePosition.w;
        const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        if (all(uv >= float2(0.0)) && all(uv <= float2(1.0)) && ndc.z >= 0.0 && ndc.z <= 1.0) {
            const float2 texel = 1.0 / float2(shadowMap.get_width(), shadowMap.get_height());
            float occlusion = 0.0;
            for (int y=-1; y<=1; ++y) for (int x=-1; x<=1; ++x) {
                const float4 storedSample = shadowMap.sample(
                    shadowSampler, uv + float2(float(x),float(y))*texel);
                const float stored = input.shadowDepthPacked > 0.5
                    ? dot(storedSample, float4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 0.0))
                    : storedSample.r;
                occlusion += ndc.z - 0.0025 > stored ? 1.0 : 0.0;
            }
            shadowVisibility = 1.0 - (occlusion / 9.0) * 0.72;
        }
    }
    const float3 directionalContribution = directionalBrdf * nDotL * float3(1.15,1.15,1.15) * shadowVisibility;

    const float3 toPointLight = input.pointLightPositionRange.xyz - input.worldPosition;
    const float lightDistance = length(toPointLight);
    const float3 pointDirection = lightDistance > 0.0001
        ? toPointLight / lightDistance : normal;
    const float lightRange = max(input.pointLightPositionRange.w, 0.001);
    const float rangeFade = max(1.0 - lightDistance / lightRange, 0.0);
    const float attenuation = rangeFade * rangeFade /
        (1.0 + 0.18 * lightDistance * lightDistance);
    const float pointNdotL = max(dot(normal, pointDirection), 0.0);
    const float3 pointHalf = normalize(pointDirection + viewDirection);
    const float pointNdotH = max(dot(normal, pointHalf), 0.0);
    const float pointVdotH = max(dot(viewDirection, pointHalf), 0.0);
    const float pointDenominator =
        pointNdotH * pointNdotH * (alphaSquared - 1.0) + 1.0;
    const float pointDistribution = alphaSquared /
        max(3.14159265 * pointDenominator * pointDenominator, 0.001);
    const float pointGeometryL =
        pointNdotL / (pointNdotL * (1.0 - k) + k);
    const float3 pointFresnel = f0 + (1.0 - f0) * pow(1.0 - pointVdotH, 5.0);
    const float3 pointSpecular =
        pointDistribution * geometryV * pointGeometryL * pointFresnel /
        max(4.0 * nDotV * max(pointNdotL, 0.001), 0.001);
    const float3 pointDiffuseWeight = (1.0 - pointFresnel) * (1.0 - metallic);
    const float3 pointBrdf =
        pointDiffuseWeight * albedo / 3.14159265 + pointSpecular;
    const float3 pointRadiance = input.pointLightColorIntensity.rgb *
        input.pointLightColorIntensity.w * attenuation;
    const float3 pointContribution = pointBrdf * pointNdotL * pointRadiance;

    const bool emissiveSurface = input.isEmissive > 0.5;
    const float3 emission = emissiveSurface
        ? albedo * 1.35
        : (floorSurface ? albedo * 0.035 : float3(0.0));
    const float ambientStrength = floorSurface ? 0.16 : (emissiveSurface ? 0.08 : 0.12);
    const float3 ambient = albedo * ambientStrength * (1.0 - metallic);

    return float4(ambient + directionalContribution + pointContribution + emission, 1.0);
}
)metal";


struct OverlayConstants {
    float center[2];
    float radius[2];
    float color[4];
    float viewport[2];
    Uint32 shape;
    Uint32 padding;
};


constexpr char shadowVertexSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;
struct ShadowInput { float3 position [[attribute(0)]]; };
struct ShadowOutput { float4 position [[position]]; float depthPacked; };
struct ShadowConstants { float4x4 mvp; float4x4 model; float4 depthFlags; };
vertex ShadowOutput shadow_vertex(ShadowInput in [[stage_in]], constant ShadowConstants& c [[buffer(0)]]) {
    ShadowOutput output;
    output.position = c.mvp * float4(in.position, 1.0);
    output.depthPacked = c.depthFlags.x;
    return output;
}
)metal";
constexpr char shadowFragmentSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;
struct ShadowOutput { float4 position [[position]]; float depthPacked; };
float4 packDepth24(float depth) {
    const float safeDepth = clamp(depth, 0.0, 1.0 - 1.0 / 16581375.0);
    float3 encoded = fract(safeDepth * float3(1.0, 255.0, 65025.0));
    encoded -= encoded.yzz * float3(1.0 / 255.0, 1.0 / 255.0, 0.0);
    return float4(encoded, 1.0);
}
fragment float4 shadow_fragment(ShadowOutput input [[stage_in]]) {
    return input.depthPacked > 0.5
        ? packDepth24(input.position.z)
        : float4(input.position.z, 0.0, 0.0, 1.0);
}
)metal";

constexpr char bloomVertexSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;
struct BloomVertexOutput {
    float4 position [[position]];
    float2 uv;
};
vertex BloomVertexOutput bloom_vertex(uint vertexId [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)
    };
    BloomVertexOutput output;
    const float2 position = positions[vertexId];
    output.position = float4(position, 0.0, 1.0);
    output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
    return output;
}
)metal";

constexpr char bloomFragmentSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;
struct BloomVertexOutput {
    float4 position [[position]];
    float2 uv;
};
struct CompositeConstants {
    float4x4 inverseViewProjection;
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 depthFlags;
};

float3 reconstructWorldPosition(float2 uv, float depth, constant CompositeConstants& c)
{
    const float2 ndcXY = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 world = c.inverseViewProjection * float4(ndcXY, depth, 1.0);
    return world.xyz / max(abs(world.w), 0.00001) * (world.w < 0.0 ? -1.0 : 1.0);
}

struct BloomFragmentOutput {
    float4 color [[color(0)]];
    float4 diagnostics [[color(1)]];
};

fragment BloomFragmentOutput bloom_fragment(
    BloomVertexOutput input [[stage_in]],
    texture2d<float> sceneTexture [[texture(0)]],
    sampler sceneSampler [[sampler(0)]],
    texture2d<float> depthTexture [[texture(1)]],
    sampler depthSampler [[sampler(1)]],
    constant CompositeConstants& constants [[buffer(0)]])
{
    const float2 texel = 1.0 / float2(sceneTexture.get_width(), sceneTexture.get_height());
    const float2 uv = input.uv;
    const float4 centerDepthSample = depthTexture.sample(depthSampler, uv);
    const float centerDepth = constants.depthFlags.x > 0.5
        ? dot(centerDepthSample, float4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 0.0))
        : centerDepthSample.r;
    float3 base = sceneTexture.sample(sceneSampler, uv).rgb;
    float3 reflectionColor = float3(0.0);
    float reflectionWeight = 0.0;
    float diagnosticReceiverMask = 0.0;
    float diagnosticRayMarchSteps = 0.0;
    float diagnosticRayHit = 0.0;
    float2 diagnosticHitUV = float2(0.0);

    // Reconstruct a world-space position from the camera depth prepass.
    // Neighbor positions provide a screen-space normal estimate without
    // requiring a second normal-buffer render target.
    if (constants.depthFlags.y > 0.5 && centerDepth < 0.999) {
        const float3 worldPosition = reconstructWorldPosition(uv, centerDepth, constants);

        // The current demo has one known reflective surface: the horizontal
        // floor plane at world Y = -1.5. Do not infer reflective materials
        // from screen-space depth normals: that incorrectly marks the cube's
        // top/bottom faces and pyramid facets as reflective too.
        const float floorDistance = abs(worldPosition.y + 1.5);
        const float surfaceMask = 1.0 - smoothstep(0.025, 0.09, floorDistance);
        diagnosticReceiverMask = surfaceMask;
        // Only reflective floor pixels need an SSR trace. The previous code
        // marched every visible depth pixel, including cube/pyramid surfaces,
        // even though their final reflection weight was zero.
        if (surfaceMask > 0.001) {
        const float3 normal = float3(0.0, 1.0, 0.0);
        const float3 viewDirection = normalize(constants.cameraPosition.xyz - worldPosition);
        const float3 incident = -viewDirection;
        const float3 reflectedDirection = normalize(reflect(incident, normal));
        // Offset away from the source surface and skip the near-field region
        // where adjacent depth discontinuities otherwise create false hits.
        const float3 rayOrigin = worldPosition + normal * 0.075;
        const float edgeFade = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
        const float screenFade = smoothstep(0.025, 0.14, edgeFade);
        float previousRayDistance = 0.0;
        float previousDepthDelta = -1.0;
        float hitDistance = 0.0;
        bool hasPreviousSample = false;
        float2 hitUV = float2(0.0);
        bool hitFound = false;
        // Thickness is measured in world units, not nonlinear projected-depth units.
        // The lower bound rejects precision noise; the upper bound rejects hits
        // that are too far behind the visible surface.
        const float minHitThickness = 0.002;
        const float maxHitThickness = 0.08;

        // March in world space, then compare the ray point with the scene point
        // reconstructed from the depth sample at the projected screen position.
        for (uint step = 1; step <= 64; ++step) {
            diagnosticRayMarchSteps = float(step);
            const float distanceAlongRay = 0.12 + 0.035 * float(step - 1);
            const float3 samplePosition = rayOrigin + reflectedDirection * distanceAlongRay;
            const float4 clip = constants.viewProjection * float4(samplePosition, 1.0);
            if (clip.w <= 0.001) break;
            const float3 projected = clip.xyz / clip.w;
            const float2 sampleUV = float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
            if (any(sampleUV <= float2(0.003)) || any(sampleUV >= float2(0.997)) ||
                projected.z <= 0.0 || projected.z >= 1.0) break;

            const float4 sceneDepthSample = depthTexture.sample(depthSampler, sampleUV);
            const float sceneDepth = constants.depthFlags.x > 0.5
                ? dot(sceneDepthSample, float4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 0.0))
                : sceneDepthSample.r;
            if (sceneDepth < 0.999) {
                const float3 sceneWorldPosition =
                    reconstructWorldPosition(sampleUV, sceneDepth, constants);
                const float rayCameraDistance =
                    length(samplePosition - constants.cameraPosition.xyz);
                const float sceneCameraDistance =
                    length(sceneWorldPosition - constants.cameraPosition.xyz);
                const float depthDelta = rayCameraDistance - sceneCameraDistance;

                // A hit must cross from in-front to behind the visible depth.
                // Do not apply the maximum thickness until after refinement:
                // a coarse step can land farther behind than the accepted band.
                if (hasPreviousSample && depthDelta > minHitThickness &&
                    previousDepthDelta <= minHitThickness) {
                    float lowDistance = previousRayDistance;
                    float highDistance = distanceAlongRay;

                    // Four binary steps reduce the 0.035-unit march interval
                    // to roughly 0.0022 world units without another full trace.
                    for (uint refine = 0; refine < 4; ++refine) {
                        const float middleDistance = (lowDistance + highDistance) * 0.5;
                        const float3 middlePosition =
                            rayOrigin + reflectedDirection * middleDistance;
                        const float4 middleClip =
                            constants.viewProjection * float4(middlePosition, 1.0);
                        if (middleClip.w <= 0.001) break;
                        const float3 middleProjected = middleClip.xyz / middleClip.w;
                        const float2 middleUV = float2(
                            middleProjected.x * 0.5 + 0.5,
                            0.5 - middleProjected.y * 0.5);
                        if (any(middleUV <= float2(0.003)) ||
                            any(middleUV >= float2(0.997))) break;

                        const float4 middleDepthSample =
                            depthTexture.sample(depthSampler, middleUV);
                        const float middleSceneDepth = constants.depthFlags.x > 0.5
                            ? dot(middleDepthSample,
                                float4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 0.0))
                            : middleDepthSample.r;
                        if (middleSceneDepth < 0.999) {
                            const float3 middleSceneWorldPosition =
                                reconstructWorldPosition(
                                    middleUV, middleSceneDepth, constants);
                            const float middleDelta =
                                length(middlePosition - constants.cameraPosition.xyz) -
                                length(middleSceneWorldPosition - constants.cameraPosition.xyz);
                            if (middleDelta > minHitThickness) {
                                highDistance = middleDistance;
                            } else {
                                lowDistance = middleDistance;
                            }
                        } else {
                            // Background is in front of any geometry at this UV.
                            lowDistance = middleDistance;
                        }
                    }

                    // Re-evaluate the refined high endpoint and apply the
                    // world-space thickness interval to the final candidate.
                    const float3 refinedPosition =
                        rayOrigin + reflectedDirection * highDistance;
                    const float4 refinedClip =
                        constants.viewProjection * float4(refinedPosition, 1.0);
                    if (refinedClip.w > 0.001) {
                        const float3 refinedProjected = refinedClip.xyz / refinedClip.w;
                        const float2 candidateUV = float2(
                            refinedProjected.x * 0.5 + 0.5,
                            0.5 - refinedProjected.y * 0.5);
                        if (all(candidateUV > float2(0.003)) &&
                            all(candidateUV < float2(0.997))) {
                            const float4 candidateDepthSample =
                                depthTexture.sample(depthSampler, candidateUV);
                            const float candidateDepth = constants.depthFlags.x > 0.5
                                ? dot(candidateDepthSample,
                                    float4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 0.0))
                                : candidateDepthSample.r;
                            if (candidateDepth < 0.999) {
                                const float3 candidateWorldPosition =
                                    reconstructWorldPosition(
                                        candidateUV, candidateDepth, constants);
                                const float candidateDelta =
                                    length(refinedPosition - constants.cameraPosition.xyz) -
                                    length(candidateWorldPosition - constants.cameraPosition.xyz);
                                if (candidateDelta > minHitThickness &&
                                    candidateDelta <= maxHitThickness) {
                                    hitUV = candidateUV;
                                    hitDistance = highDistance;
                                    hitFound = true;
                                }
                            }
                        }
                    }
                    if (hitFound) break;
                }

                previousDepthDelta = depthDelta;
                hasPreviousSample = true;
                previousRayDistance = distanceAlongRay;
            } else {
                // Keep the previous-sample sentinel in the same world-space
                // distance-delta domain as valid depth samples.
                previousDepthDelta = -1.0;
                hasPreviousSample = true;
                previousRayDistance = distanceAlongRay;
            }
        }

        if (hitFound) {
            diagnosticRayHit = 1.0;
            diagnosticHitUV = hitUV;
            reflectionColor = sceneTexture.sample(sceneSampler, hitUV).rgb;
            const float distanceFade = 1.0 - clamp(hitDistance / 3.0, 0.0, 1.0);
            reflectionWeight = surfaceMask * screenFade *
                distanceFade * 0.22;
        }
        }
    }

    base = mix(base, reflectionColor, reflectionWeight);

    // Preserve the existing depth-neighborhood ambient-occlusion approximation.
    const float2 aoOffsets[8] = {
        float2(1.5, 0.0), float2(-1.5, 0.0),
        float2(0.0, 1.5), float2(0.0, -1.5),
        float2(2.5, 2.5), float2(-2.5, 2.5),
        float2(2.5, -2.5), float2(-2.5, -2.5)
    };
    float occlusion = 0.0;
    if (centerDepth < 0.999) {
        for (uint index = 0; index < 8; ++index) {
            const float4 neighborDepthSample = depthTexture.sample(
                depthSampler, uv + aoOffsets[index] * texel);
            const float neighborDepth = constants.depthFlags.x > 0.5
                ? dot(neighborDepthSample, float4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 0.0))
                : neighborDepthSample.r;
            occlusion += smoothstep(0.0015, 0.012, centerDepth - neighborDepth);
        }
    }
    const float ambientVisibility = clamp(1.0 - (occlusion / 8.0) * 0.58, 0.58, 1.0);
    base *= ambientVisibility;

    // Existing 9-tap bloom, evaluated after reflection and SSAO.
    const float2 offsets[9] = {
        float2(0.0, 0.0),
        float2(1.5, 0.0), float2(-1.5, 0.0),
        float2(0.0, 1.5), float2(0.0, -1.5),
        float2(3.0, 0.0), float2(-3.0, 0.0),
        float2(0.0, 3.0), float2(0.0, -3.0)
    };
    const float weights[9] = {
        0.16, 0.12, 0.12, 0.12, 0.12, 0.05, 0.05, 0.05, 0.05
    };
    float3 glow = float3(0.0);
    for (uint index = 0; index < 9; ++index) {
        const float3 sampleColor = sceneTexture.sample(sceneSampler, uv + offsets[index] * texel).rgb;
        const float brightness = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
        glow += sampleColor * smoothstep(0.82, 1.20, brightness) * weights[index];
    }
    const float3 hdrColor = base + glow * 0.55;
    const float3 numerator = hdrColor * (2.51 * hdrColor + 0.03);
    const float3 denominator = hdrColor * (2.43 * hdrColor + 0.59) + 0.14;
    const float3 mapped = clamp(numerator / max(denominator, 0.0001), 0.0, 1.0);
    float3 finalColor = mapped;
    const uint debugView = uint(constants.depthFlags.z + 0.5);
    if (debugView == 1) {
        finalColor = centerDepth < 0.999
            ? float3(centerDepth) : float3(0.0);
    } else if (debugView == 2) {
        finalColor = float3(diagnosticReceiverMask,
                            1.0 - diagnosticReceiverMask, 0.0);
    } else if (debugView == 3) {
        finalColor = float3(diagnosticRayHit,
                            diagnosticReceiverMask, 0.0);
    } else if (debugView == 4) {
        finalColor = diagnosticRayHit > 0.5
            ? float3(diagnosticHitUV, 1.0) : float3(0.0);
    }
    BloomFragmentOutput output;
    output.color = float4(finalColor, 1.0);
    output.diagnostics = float4(
        centerDepth < 0.999 ? 1.0 : 0.0,
        diagnosticReceiverMask,
        diagnosticRayMarchSteps / 64.0,
        diagnosticRayHit);
    return output;
}
)metal";

constexpr char overlayVertexSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;
struct OverlayConstants {
    float2 center;
    float2 radius;
    float4 color;
    float2 viewport;
    uint shape;
    uint padding;
};
struct OverlayOutput {
    float4 position [[position]];
    float4 color;
};
vertex OverlayOutput overlay_vertex(
    uint vertexId [[vertex_id]],
    constant OverlayConstants& constants [[buffer(0)]])
{
    OverlayOutput output;
    float2 local;
    if (constants.shape == 0) {
        const uint segment = vertexId / 6;
        const uint corner = vertexId % 6;
        const float angles[4] = {
            float(segment) / 32.0 * 6.28318530718,
            float(segment) / 32.0 * 6.28318530718,
            float(segment + 1) / 32.0 * 6.28318530718,
            float(segment) / 32.0 * 6.28318530718
        };
        const float angle0 = angles[0];
        const float angle1 = angles[2];
        const float2 outer0 = float2(cos(angle0), sin(angle0));
        const float thickness = max(1.0 / max(constants.radius.x, constants.radius.y), 0.001);
        const float innerScale = 1.0 - thickness;
        const float2 inner0 = outer0 * innerScale;
        const float2 outer1 = float2(cos(angle1), sin(angle1));
        const float2 inner1 = outer1 * innerScale;
        if (corner == 0 || corner == 3) local = outer0;
        else if (corner == 1) local = inner0;
        else if (corner == 2 || corner == 4) local = inner1;
        else local = outer1;
    } else {
        const float2 quad[6] = {
            float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
            float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
        };
        local = quad[vertexId];
    }
    const float2 pixel = constants.center + local * constants.radius;
    output.position = float4(
        pixel.x / constants.viewport.x * 2.0 - 1.0,
        1.0 - pixel.y / constants.viewport.y * 2.0, 0.0, 1.0);
    output.color = constants.color;
    return output;
}
)metal";

constexpr char overlayFragmentSource[] = R"metal(
#include <metal_stdlib>
using namespace metal;
struct OverlayOutput {
    float4 position [[position]];
    float4 color;
};
fragment float4 overlay_fragment(OverlayOutput input [[stage_in]])
{
    return input.color;
}
)metal";

Matrix4 identityMatrix() {
    Matrix4 result{};
    result[0] = 1.0F;
    result[5] = 1.0F;
    result[10] = 1.0F;
    result[15] = 1.0F;
    return result;
}

Matrix4 multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result[static_cast<std::size_t>(column * 4 + row)] +=
                    a[static_cast<std::size_t>(k * 4 + row)] *
                    b[static_cast<std::size_t>(column * 4 + k)];
            }
        }
    }
    return result;
}

Matrix4 inverseMatrix(const Matrix4& matrix) {
    float augmented[4][8]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            augmented[row][column] = matrix[static_cast<std::size_t>(column * 4 + row)];
        }
        augmented[row][row + 4] = 1.0F;
    }
    for (int pivot = 0; pivot < 4; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < 4; ++row) {
            if (std::abs(augmented[row][pivot]) > std::abs(augmented[best][pivot])) best = row;
        }
        if (std::abs(augmented[best][pivot]) < 1.0e-8F) return identityMatrix();
        if (best != pivot) {
            for (int column = 0; column < 8; ++column) std::swap(augmented[pivot][column], augmented[best][column]);
        }
        const float divisor = augmented[pivot][pivot];
        for (int column = 0; column < 8; ++column) augmented[pivot][column] /= divisor;
        for (int row = 0; row < 4; ++row) {
            if (row == pivot) continue;
            const float factor = augmented[row][pivot];
            for (int column = 0; column < 8; ++column) augmented[row][column] -= factor * augmented[pivot][column];
        }
    }
    Matrix4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result[static_cast<std::size_t>(column * 4 + row)] = augmented[row][column + 4];
        }
    }
    return result;
}

Matrix4 translationMatrix(const Vec3& position) {
    Matrix4 result = identityMatrix();
    result[12] = position.x;
    result[13] = position.y;
    result[14] = position.z;
    return result;
}

Matrix4 scaleMatrix(const Vec3& scale) {
    Matrix4 result{};
    result[0] = scale.x;
    result[5] = scale.y;
    result[10] = scale.z;
    result[15] = 1.0F;
    return result;
}

Matrix4 rotationX(float angle) {
    Matrix4 result = identityMatrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    result[5] = c;
    result[9] = -s;
    result[6] = s;
    result[10] = c;
    return result;
}

Matrix4 rotationY(float angle) {
    Matrix4 result = identityMatrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    result[0] = c;
    result[8] = s;
    result[2] = -s;
    result[10] = c;
    return result;
}

Matrix4 rotationZ(float angle) {
    Matrix4 result = identityMatrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    result[0] = c;
    result[4] = -s;
    result[1] = s;
    result[5] = c;
    return result;
}

Matrix4 modelMatrix(const Transform& transform) {
    const Matrix4 rotations = multiply(
        rotationZ(transform.rotation.z),
        multiply(rotationY(transform.rotation.y), rotationX(transform.rotation.x)));
    return multiply(translationMatrix(transform.position),
                    multiply(rotations, scaleMatrix(transform.scale)));
}

Matrix4 normalMatrix(const Transform& transform) {
    const Matrix4 model = modelMatrix(transform);
    const float a00 = model[0], a01 = model[4], a02 = model[8];
    const float a10 = model[1], a11 = model[5], a12 = model[9];
    const float a20 = model[2], a21 = model[6], a22 = model[10];
    const float c00 = a11 * a22 - a12 * a21;
    const float c01 = a12 * a20 - a10 * a22;
    const float c02 = a10 * a21 - a11 * a20;
    const float c10 = a02 * a21 - a01 * a22;
    const float c11 = a00 * a22 - a02 * a20;
    const float c12 = a01 * a20 - a00 * a21;
    const float c20 = a01 * a12 - a02 * a11;
    const float c21 = a02 * a10 - a00 * a12;
    const float c22 = a00 * a11 - a01 * a10;
    const float determinant = a00 * c00 + a01 * c01 + a02 * c02;

    Matrix4 result = identityMatrix();
    if (std::abs(determinant) <= 1.0e-8F) {
        return result;
    }
    const float inverseDeterminant = 1.0F / determinant;
    result[0] = c00 * inverseDeterminant;
    result[4] = c01 * inverseDeterminant;
    result[8] = c02 * inverseDeterminant;
    result[1] = c10 * inverseDeterminant;
    result[5] = c11 * inverseDeterminant;
    result[9] = c12 * inverseDeterminant;
    result[2] = c20 * inverseDeterminant;
    result[6] = c21 * inverseDeterminant;
    result[10] = c22 * inverseDeterminant;
    return result;
}

Matrix4 shadowViewProjection() {
    const Vec3 eye{-4.5F,8.0F,-5.0F}, target{0.0F,0.0F,0.0F};
    const auto norm=[](Vec3 v){const float n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);return n>1e-5F?Vec3{v.x/n,v.y/n,v.z/n}:Vec3{};};
    const auto dot=[](Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const Vec3 f=norm({target.x-eye.x,target.y-eye.y,target.z-eye.z}),r=norm({f.z,0.0F,-f.x});
    const Vec3 u=norm({f.y*r.z-f.z*r.y,f.z*r.x-f.x*r.z,f.x*r.y-f.y*r.x});
    Matrix4 v=identityMatrix();v[0]=r.x;v[4]=r.y;v[8]=r.z;v[1]=u.x;v[5]=u.y;v[9]=u.z;
    v[2]=f.x;v[6]=f.y;v[10]=f.z;v[12]=-dot(r,eye);v[13]=-dot(u,eye);v[14]=-dot(f,eye);
    constexpr float e=8.0F,n=0.1F,z=30.0F;Matrix4 p=identityMatrix();
    p[0]=1.0F/e;p[5]=1.0F/e;p[10]=1.0F/(z-n);p[14]=-n/(z-n);return multiply(p,v);
}

Matrix4 viewMatrix(const Camera& camera) {
    const float yaw = camera.yaw();
    const float pitch = camera.pitch();
    const float sinYaw = std::sin(yaw);
    const float cosYaw = std::cos(yaw);
    const float sinPitch = std::sin(pitch);
    const float cosPitch = std::cos(pitch);

    const Vec3 right{cosYaw, 0.0F, -sinYaw};
    const Vec3 up{-sinYaw * sinPitch, cosPitch, -cosYaw * sinPitch};
    const Vec3 forward{sinYaw * cosPitch, sinPitch, cosYaw * cosPitch};
    const Vec3 position{camera.x(), camera.y(), camera.z()};

    Matrix4 result = identityMatrix();
    result[0] = right.x;   result[4] = right.y;   result[8] = right.z;
    result[1] = up.x;      result[5] = up.y;      result[9] = up.z;
    result[2] = forward.x; result[6] = forward.y; result[10] = forward.z;
    result[12] = -(right.x * position.x + right.y * position.y + right.z * position.z);
    result[13] = -(up.x * position.x + up.y * position.y + up.z * position.z);
    result[14] = -(forward.x * position.x + forward.y * position.y + forward.z * position.z);
    return result;
}

Matrix4 projectionMatrix(float aspectRatio) {
    constexpr float nearPlane = 0.1F;
    constexpr float farPlane = 100.0F;
    // Match Renderer3D's CPU projection: min(width, height) * 0.42 * 2.4.
    // SDL_GPU uses normalized device coordinates, so this is the equivalent
    // vertical focal length for the same screen-space cube size.
    constexpr float focalLength = 2.016F;

    Matrix4 result{};
    result[0] = focalLength / std::max(aspectRatio, 0.001F);
    result[5] = focalLength;
    result[10] = farPlane / (farPlane - nearPlane);
    result[11] = 1.0F;
    result[14] = -(nearPlane * farPlane) / (farPlane - nearPlane);
    return result;
}

bool buildGpuGeometry(const Mesh& mesh, std::vector<GpuVertex>& vertices,
                      std::vector<std::uint32_t>& indices, std::string& error) {
    if (mesh.empty()) {
        error = "Cannot upload an empty mesh";
        return false;
    }

    vertices.clear();
    indices.clear();
    if (mesh.triangles.size() > std::numeric_limits<std::uint32_t>::max() / 3U) {
        error = "Mesh has too many triangle indices for SDL_GPU";
        return false;
    }
    vertices.reserve(mesh.triangles.size() * 3U);
    indices.reserve(mesh.triangles.size() * 3U);

    for (const MeshTriangle& triangle : mesh.triangles) {
        const std::size_t ia = triangle.indices[0];
        const std::size_t ib = triangle.indices[1];
        const std::size_t ic = triangle.indices[2];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() ||
            ic >= mesh.vertices.size()) {
            error = "Mesh triangle contains an out-of-range vertex index";
            return false;
        }

        const MeshVertex& a = mesh.vertices[ia];
        const MeshVertex& b = mesh.vertices[ib];
        const MeshVertex& c = mesh.vertices[ic];
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float abz = b.z - a.z;
        const float acx = c.x - a.x;
        const float acy = c.y - a.y;
        const float acz = c.z - a.z;
        const float nx = aby * acz - abz * acy;
        const float ny = abz * acx - abx * acz;
        const float nz = abx * acy - aby * acx;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length <= 1.0e-7F) {
            continue;
        }

        const float faceNormal[3]{nx / length, ny / length, nz / length};
        // A varying is interpolated across a primitive. Keep UV presence constant
        // for all three duplicated vertices so mixed OBJ corners cannot create
        // partially textured fragments within one triangle.
        const bool triangleHasTexCoords = gpu_detail::triangleHasCompleteTexCoords(
            a.hasTexCoord, b.hasTexCoord, c.hasTexCoord);
        const MeshVertex* corners[3]{&a, &b, &c};
        for (const MeshVertex* corner : corners) {
            GpuVertex vertex{};
            vertex.position[0] = corner->x;
            vertex.position[1] = corner->y;
            vertex.position[2] = corner->z;
            float selectedNormal[3]{faceNormal[0], faceNormal[1], faceNormal[2]};
            if (corner->hasNormal) {
                const float normalLength = std::sqrt(
                    corner->nx * corner->nx + corner->ny * corner->ny + corner->nz * corner->nz);
                if (normalLength > 1.0e-7F) {
                    selectedNormal[0] = corner->nx / normalLength;
                    selectedNormal[1] = corner->ny / normalLength;
                    selectedNormal[2] = corner->nz / normalLength;
                }
            }
            std::copy(std::begin(selectedNormal), std::end(selectedNormal),
                      std::begin(vertex.normal));
            vertex.color[0] = 1.0F;
            vertex.color[1] = 1.0F;
            vertex.color[2] = 1.0F;
            vertex.uv[0] = triangleHasTexCoords ? corner->u : 0.0F;
            vertex.uv[1] = triangleHasTexCoords ? 1.0F - corner->v : 0.0F;
            vertex.hasTexCoord = triangleHasTexCoords ? 1.0F : 0.0F;
            const auto index = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(vertex);
            indices.push_back(index);
        }
    }

    if (indices.empty()) {
        error = "Mesh contains no non-degenerate triangles";
        return false;
    }
    if (vertices.size() > std::numeric_limits<Uint32>::max() / sizeof(GpuVertex) ||
        indices.size() > std::numeric_limits<Uint32>::max() / sizeof(std::uint32_t)) {
        error = "Mesh GPU buffers exceed SDL_GPU buffer size limits";
        return false;
    }
    return true;
}

bool uploadBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* destination,
                  const void* data, Uint32 size, std::string& error) {
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (transfer == nullptr) {
        error = std::string("SDL_CreateGPUTransferBuffer failed: ") + SDL_GetError();
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == nullptr) {
        error = std::string("SDL_MapGPUTransferBuffer failed: ") + SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
    if (commandBuffer == nullptr) {
        error = std::string("SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (copyPass == nullptr) {
        error = std::string("SDL_BeginGPUCopyPass failed: ") + SDL_GetError();
        (void)SDL_CancelGPUCommandBuffer(commandBuffer);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = transfer;
    SDL_GPUBufferRegion target{};
    target.buffer = destination;
    target.size = size;
    SDL_UploadToGPUBuffer(copyPass, &source, &target, false);
    SDL_EndGPUCopyPass(copyPass);

    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        error = std::string("SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError();
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

} // namespace

GpuRenderer::~GpuRenderer() {
    shutdown();
}

void GpuRenderer::setError(const char* operation) {
    lastError_ = std::string(operation) + " failed: " + SDL_GetError();
}

bool GpuRenderer::initialize(SDL_Window* window, const std::vector<const Mesh*>& meshes, const RgbaImage& texture) {
    shutdown();
    lastError_.clear();
    driverName_.clear();
    if (window == nullptr) {
        lastError_ = "Cannot initialize GPU renderer without a window";
        return false;
    }
    if (meshes.empty()) {
        lastError_ = "Cannot initialize GPU renderer without any meshes";
        return false;
    }

    std::vector<GpuVertex> vertices;
    std::vector<std::uint32_t> indices;
    meshRanges_.clear();
    for (const Mesh* mesh : meshes) {
        if (mesh == nullptr) {
            lastError_ = "GPU mesh list contains a null mesh";
            return false;
        }
        std::vector<GpuVertex> meshVertices;
        std::vector<std::uint32_t> meshIndices;
        if (!buildGpuGeometry(*mesh, meshVertices, meshIndices, lastError_)) {
            return false;
        }
        if (vertices.size() > std::numeric_limits<Uint32>::max() / sizeof(GpuVertex) -
                meshVertices.size() ||
            indices.size() > std::numeric_limits<Uint32>::max() / sizeof(std::uint32_t) -
                meshIndices.size()) {
            lastError_ = "Combined GPU mesh buffers exceed SDL_GPU buffer size limits";
            return false;
        }
        const auto firstIndex = static_cast<Uint32>(indices.size());
        const auto baseVertex = static_cast<std::uint32_t>(vertices.size());
        for (const std::uint32_t index : meshIndices) {
            if (index > std::numeric_limits<std::uint32_t>::max() - baseVertex) {
                lastError_ = "Combined GPU mesh vertex index overflow";
                return false;
            }
            indices.push_back(index + baseVertex);
        }
        vertices.insert(vertices.end(), meshVertices.begin(), meshVertices.end());
        meshRanges_.push_back({firstIndex, static_cast<Uint32>(meshIndices.size())});
    }
    if (!texture.hasValidRgba8Data()) {
        lastError_ =
            "GPU texture must contain exactly width * height * 4 RGBA8 bytes "
            "within SDL_GPU transfer buffer size limits";
        return false;
    }

    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (device_ == nullptr) {
        setError("SDL_CreateGPUDevice");
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device_, window)) {
        setError("SDL_ClaimWindowForGPUDevice");
        shutdown();
        return false;
    }
    window_ = window;
    const char* driver = SDL_GetGPUDeviceDriver(device_);
    driverName_ = driver != nullptr ? driver : "Unknown";

    if (SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    } else if (SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    } else {
        lastError_ = "GPU device does not support a usable depth-stencil texture format";
        shutdown();
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) {
        lastError_ = "GPU device does not support the RGBA8 SSR diagnostics target";
        shutdown();
        return false;
    }
    const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    sceneColorFormat_ = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;    const SDL_GPUTextureUsageFlags hdrUsage =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!SDL_GPUTextureSupportsFormat(device_, sceneColorFormat_,
            SDL_GPU_TEXTURETYPE_2D, hdrUsage)) {
        // Some devices cannot sample a floating-point render target. Keep the
        // original swapchain-format path rather than failing GPU initialization.
        sceneColorFormat_ = swapchainFormat;
    }
    sampleCount_ = SDL_GPU_SAMPLECOUNT_1;
    if (SDL_GPUTextureSupportsFormat(device_, sceneColorFormat_, SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) &&
        SDL_GPUTextureSupportsSampleCount(device_, sceneColorFormat_, SDL_GPU_SAMPLECOUNT_4) &&
        SDL_GPUTextureSupportsSampleCount(device_, depthFormat_, SDL_GPU_SAMPLECOUNT_4)) {
        sampleCount_ = SDL_GPU_SAMPLECOUNT_4;
    }

    SDL_GPUBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertexBufferInfo.size = static_cast<Uint32>(vertices.size() * sizeof(GpuVertex));
    vertexBuffer_ = SDL_CreateGPUBuffer(device_, &vertexBufferInfo);
    if (vertexBuffer_ == nullptr) {
        setError("SDL_CreateGPUBuffer(vertex)");
        shutdown();
        return false;
    }
    if (!uploadBuffer(device_, vertexBuffer_, vertices.data(), vertexBufferInfo.size, lastError_)) {
        shutdown();
        return false;
    }

    SDL_GPUBufferCreateInfo indexBufferInfo{};
    indexBufferInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    indexBufferInfo.size = static_cast<Uint32>(indices.size() * sizeof(std::uint32_t));
    indexBuffer_ = SDL_CreateGPUBuffer(device_, &indexBufferInfo);
    if (indexBuffer_ == nullptr) {
        setError("SDL_CreateGPUBuffer(index)");
        shutdown();
        return false;
    }
    if (!uploadBuffer(device_, indexBuffer_, indices.data(), indexBufferInfo.size, lastError_)) {
        shutdown();
        return false;
    }

    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = texture.width;
    textureInfo.height = texture.height;
    textureInfo.layer_count_or_depth = 1;

    // Mipmaps reduce aliasing and shimmering when the texture is viewed at a distance.
    Uint32 mipLevels = 1;
    for (Uint32 dimension = std::max(texture.width, texture.height);
         dimension > 1; dimension /= 2) {
        ++mipLevels;
    }
    const SDL_GPUTextureUsageFlags mipmapUsage =
        SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    const bool supportsMipmaps = SDL_GPUTextureSupportsFormat(
        device_, textureInfo.format, textureInfo.type, mipmapUsage);
    if (supportsMipmaps) {
        textureInfo.usage = mipmapUsage;
        textureInfo.num_levels = mipLevels;
    } else {
        textureInfo.num_levels = 1;
        std::fprintf(stderr,
            "[OriginalEngine] GPU format does not support render-target mipmaps; using base level only.\n");
    }
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    colorTexture_ = SDL_CreateGPUTexture(device_, &textureInfo);
    if (colorTexture_ == nullptr) {
        setError("SDL_CreateGPUTexture(color)");
        shutdown();
        return false;
    }

    SDL_GPUTransferBufferCreateInfo textureTransferInfo{};
    textureTransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    textureTransferInfo.size = static_cast<Uint32>(texture.pixels.size());
    SDL_GPUTransferBuffer* textureTransfer =
        SDL_CreateGPUTransferBuffer(device_, &textureTransferInfo);
    if (textureTransfer == nullptr) {
        setError("SDL_CreateGPUTransferBuffer(texture)");
        shutdown();
        return false;
    }
    void* textureMapped = SDL_MapGPUTransferBuffer(device_, textureTransfer, false);
    if (textureMapped == nullptr) {
        setError("SDL_MapGPUTransferBuffer(texture)");
        SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);
        shutdown();
        return false;
    }
    std::memcpy(textureMapped, texture.pixels.data(), texture.pixels.size());
    SDL_UnmapGPUTransferBuffer(device_, textureTransfer);

    SDL_GPUCommandBuffer* textureCommandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (textureCommandBuffer == nullptr) {
        setError("SDL_AcquireGPUCommandBuffer(texture)");
        SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);
        shutdown();
        return false;
    }
    SDL_GPUCopyPass* textureCopyPass = SDL_BeginGPUCopyPass(textureCommandBuffer);
    if (textureCopyPass == nullptr) {
        setError("SDL_BeginGPUCopyPass(texture)");
        (void)SDL_CancelGPUCommandBuffer(textureCommandBuffer);
        SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);
        shutdown();
        return false;
    }
    SDL_GPUTextureTransferInfo textureSource{};
    textureSource.transfer_buffer = textureTransfer;
    textureSource.pixels_per_row = texture.width;
    textureSource.rows_per_layer = texture.height;
    SDL_GPUTextureRegion textureDestination{};
    textureDestination.texture = colorTexture_;
    textureDestination.w = texture.width;
    textureDestination.h = texture.height;
    textureDestination.d = 1;
    SDL_UploadToGPUTexture(textureCopyPass, &textureSource, &textureDestination, false);
    SDL_EndGPUCopyPass(textureCopyPass);
    if (supportsMipmaps) {
        SDL_GenerateMipmapsForGPUTexture(textureCommandBuffer, colorTexture_);
    }
    if (!SDL_SubmitGPUCommandBuffer(textureCommandBuffer)) {
        setError("SDL_SubmitGPUCommandBuffer(texture upload)");
        SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);
        shutdown();
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(device_, textureTransfer);

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.max_lod = static_cast<float>(textureInfo.num_levels - 1U);
    colorSampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
    if (colorSampler_ == nullptr) {
        setError("SDL_CreateGPUSampler");
        shutdown();
        return false;
    }

    SDL_GPUShaderCreateInfo vertexInfo{};
    vertexInfo.code_size = std::strlen(vertexSource);
    vertexInfo.code = reinterpret_cast<const Uint8*>(vertexSource);
    vertexInfo.entrypoint = "main_vertex";
    vertexInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertexInfo.num_uniform_buffers = 1;
    SDL_GPUShader* vertexShader = SDL_CreateGPUShader(device_, &vertexInfo);
    if (vertexShader == nullptr) {
        setError("SDL_CreateGPUShader(vertex)");
        shutdown();
        return false;
    }

    SDL_GPUShaderCreateInfo fragmentInfo{};
    fragmentInfo.code_size = std::strlen(fragmentSource);
    fragmentInfo.code = reinterpret_cast<const Uint8*>(fragmentSource);
    fragmentInfo.entrypoint = "main_fragment";
    fragmentInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragmentInfo.num_samplers = 2;
    SDL_GPUShader* fragmentShader = SDL_CreateGPUShader(device_, &fragmentInfo);
    if (fragmentShader == nullptr) {
        setError("SDL_CreateGPUShader(fragment)");
        SDL_ReleaseGPUShader(device_, vertexShader);
        shutdown();
        return false;
    }

    SDL_GPUVertexBufferDescription vertexBufferDescription{};
    vertexBufferDescription.slot = 0;
    vertexBufferDescription.pitch = static_cast<Uint32>(sizeof(GpuVertex));
    vertexBufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    const SDL_GPUVertexAttribute attributes[]{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(GpuVertex, position))},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(GpuVertex, normal))},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(GpuVertex, color))},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(GpuVertex, uv))},
        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, static_cast<Uint32>(offsetof(GpuVertex, hasTexCoord))}
    };

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = sceneColorFormat_;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDescription;
    pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    pipelineInfo.vertex_input_state.vertex_attributes = attributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes = 5;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.multisample_state.sample_count = sampleCount_;
    pipelineInfo.depth_stencil_state.enable_depth_test = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = true;
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.has_depth_stencil_target = true;
    pipelineInfo.target_info.depth_stencil_format = depthFormat_;

    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
    if (pipeline_ != nullptr) {
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
        wireframePipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
    }
    SDL_ReleaseGPUShader(device_, fragmentShader);
    SDL_ReleaseGPUShader(device_, vertexShader);
    if (pipeline_ == nullptr || wireframePipeline_ == nullptr) {
        setError("SDL_CreateGPUGraphicsPipeline");
        shutdown();
        return false;
    }

    if (!createShadowMap()) { shutdown(); return false; }
    if (shadowMappingEnabled_) {
        SDL_GPUShaderCreateInfo sv{}; sv.code_size=std::strlen(shadowVertexSource);
        sv.code=reinterpret_cast<const Uint8*>(shadowVertexSource); sv.entrypoint="shadow_vertex";
        sv.format=SDL_GPU_SHADERFORMAT_MSL; sv.stage=SDL_GPU_SHADERSTAGE_VERTEX; sv.num_uniform_buffers=1;
        SDL_GPUShader* vs=SDL_CreateGPUShader(device_,&sv);
        SDL_GPUShaderCreateInfo sf{}; sf.code_size=std::strlen(shadowFragmentSource);
        sf.code=reinterpret_cast<const Uint8*>(shadowFragmentSource); sf.entrypoint="shadow_fragment";
        sf.format=SDL_GPU_SHADERFORMAT_MSL; sf.stage=SDL_GPU_SHADERSTAGE_FRAGMENT;
        SDL_GPUShader* fs=vs?SDL_CreateGPUShader(device_,&sf):nullptr;
        if(!vs||!fs){if(vs)SDL_ReleaseGPUShader(device_,vs);if(fs)SDL_ReleaseGPUShader(device_,fs);setError("SDL_CreateGPUShader(shadow)");shutdown();return false;}
        SDL_GPUColorTargetDescription ct{};ct.format=shadowColorFormat_;
        SDL_GPUGraphicsPipelineCreateInfo pi{};pi.vertex_shader=vs;pi.fragment_shader=fs;
        pi.vertex_input_state.vertex_buffer_descriptions=&vertexBufferDescription;pi.vertex_input_state.num_vertex_buffers=1;
        pi.vertex_input_state.vertex_attributes=attributes;pi.vertex_input_state.num_vertex_attributes=5;
        pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;pi.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;pi.depth_stencil_state.enable_depth_test=true;
        pi.depth_stencil_state.enable_depth_write=true;pi.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.color_target_descriptions=&ct;pi.target_info.num_color_targets=1;
        pi.target_info.has_depth_stencil_target=true;pi.target_info.depth_stencil_format=depthFormat_;
        shadowPipeline_=SDL_CreateGPUGraphicsPipeline(device_,&pi);SDL_ReleaseGPUShader(device_,fs);SDL_ReleaseGPUShader(device_,vs);
        if(!shadowPipeline_){setError("SDL_CreateGPUGraphicsPipeline(shadow)");shutdown();return false;}
    }

    SDL_GPUShaderCreateInfo overlayVertexInfo{};
    overlayVertexInfo.code_size = std::strlen(overlayVertexSource);
    overlayVertexInfo.code = reinterpret_cast<const Uint8*>(overlayVertexSource);
    overlayVertexInfo.entrypoint = "overlay_vertex";
    overlayVertexInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    overlayVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    overlayVertexInfo.num_uniform_buffers = 1;
    SDL_GPUShader* overlayVertexShader = SDL_CreateGPUShader(device_, &overlayVertexInfo);
    if (overlayVertexShader == nullptr) {
        setError("SDL_CreateGPUShader(overlay vertex)");
        shutdown();
        return false;
    }

    SDL_GPUShaderCreateInfo overlayFragmentInfo{};
    overlayFragmentInfo.code_size = std::strlen(overlayFragmentSource);
    overlayFragmentInfo.code = reinterpret_cast<const Uint8*>(overlayFragmentSource);
    overlayFragmentInfo.entrypoint = "overlay_fragment";
    overlayFragmentInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    overlayFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    SDL_GPUShader* overlayFragmentShader = SDL_CreateGPUShader(device_, &overlayFragmentInfo);
    if (overlayFragmentShader == nullptr) {
        setError("SDL_CreateGPUShader(overlay fragment)");
        SDL_ReleaseGPUShader(device_, overlayVertexShader);
        shutdown();
        return false;
    }

    SDL_GPUColorTargetDescription overlayColorTargets[2]{};
    overlayColorTargets[0].format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    overlayColorTargets[0].blend_state.enable_blend = true;
    overlayColorTargets[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    overlayColorTargets[0].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    overlayColorTargets[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    overlayColorTargets[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    overlayColorTargets[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    overlayColorTargets[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    overlayColorTargets[1].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    overlayColorTargets[1].blend_state.enable_color_write_mask = true;
    overlayColorTargets[1].blend_state.color_write_mask = 0;
    SDL_GPUGraphicsPipelineCreateInfo overlayPipelineInfo{};
    overlayPipelineInfo.vertex_shader = overlayVertexShader;
    overlayPipelineInfo.fragment_shader = overlayFragmentShader;
    overlayPipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    overlayPipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    overlayPipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    overlayPipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    overlayPipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    overlayPipelineInfo.depth_stencil_state.enable_depth_test = false;
    overlayPipelineInfo.depth_stencil_state.enable_depth_write = false;
    overlayPipelineInfo.target_info.color_target_descriptions = overlayColorTargets;
    overlayPipelineInfo.target_info.num_color_targets = 2;
    overlayPipelineInfo.target_info.has_depth_stencil_target = false;
    overlayPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &overlayPipelineInfo);
    SDL_ReleaseGPUShader(device_, overlayFragmentShader);
    SDL_ReleaseGPUShader(device_, overlayVertexShader);
    if (overlayPipeline_ == nullptr) {
        setError("SDL_CreateGPUGraphicsPipeline(overlay)");
        shutdown();
        return false;
    }
    SDL_GPUShaderCreateInfo bloomVertexInfo{};
    bloomVertexInfo.code_size = std::strlen(bloomVertexSource);
    bloomVertexInfo.code = reinterpret_cast<const Uint8*>(bloomVertexSource);
    bloomVertexInfo.entrypoint = "bloom_vertex";
    bloomVertexInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    bloomVertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShader* bloomVertexShader = SDL_CreateGPUShader(device_, &bloomVertexInfo);
    if (bloomVertexShader == nullptr) {
        setError("SDL_CreateGPUShader(bloom vertex)");
        shutdown();
        return false;
    }

    SDL_GPUShaderCreateInfo bloomFragmentInfo{};
    bloomFragmentInfo.code_size = std::strlen(bloomFragmentSource);
    bloomFragmentInfo.code = reinterpret_cast<const Uint8*>(bloomFragmentSource);
    bloomFragmentInfo.entrypoint = "bloom_fragment";
    bloomFragmentInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    bloomFragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    bloomFragmentInfo.num_samplers = 2;
    bloomFragmentInfo.num_uniform_buffers = 1;
    SDL_GPUShader* bloomFragmentShader = SDL_CreateGPUShader(device_, &bloomFragmentInfo);
    if (bloomFragmentShader == nullptr) {
        setError("SDL_CreateGPUShader(bloom fragment)");
        SDL_ReleaseGPUShader(device_, bloomVertexShader);
        shutdown();
        return false;
    }

    SDL_GPUColorTargetDescription bloomColorTargets[2]{};
    bloomColorTargets[0].format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    bloomColorTargets[1].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo bloomPipelineInfo{};
    bloomPipelineInfo.vertex_shader = bloomVertexShader;
    bloomPipelineInfo.fragment_shader = bloomFragmentShader;
    bloomPipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    bloomPipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    bloomPipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    bloomPipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    bloomPipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    bloomPipelineInfo.depth_stencil_state.enable_depth_test = false;
    bloomPipelineInfo.depth_stencil_state.enable_depth_write = false;
    bloomPipelineInfo.target_info.color_target_descriptions = bloomColorTargets;
    bloomPipelineInfo.target_info.num_color_targets = 2;
    bloomPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &bloomPipelineInfo);
    SDL_ReleaseGPUShader(device_, bloomFragmentShader);
    SDL_ReleaseGPUShader(device_, bloomVertexShader);
    if (bloomPipeline_ == nullptr) {
        setError("SDL_CreateGPUGraphicsPipeline(bloom)");
        shutdown();
        return false;
    }

    SDL_GPUSamplerCreateInfo bloomSamplerInfo{};
    bloomSamplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    bloomSamplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    bloomSamplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    bloomSamplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    bloomSamplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    bloomSamplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    bloomSampler_ = SDL_CreateGPUSampler(device_, &bloomSamplerInfo);
    if (bloomSampler_ == nullptr) {
        setError("SDL_CreateGPUSampler(bloom)");
        shutdown();
        return false;
    }
    return true;
}

bool GpuRenderer::createShadowMap() {
    const SDL_GPUTextureUsageFlags usage =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    // Prefer single-channel precision, but do not make GPU activation depend
    // on R32_FLOAT render-target support. RGBA8 is the portable fallback.
    shadowColorFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    constexpr SDL_GPUTextureFormat candidates[]{
        SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
    };
    for (const SDL_GPUTextureFormat format : candidates) {
        if (SDL_GPUTextureSupportsFormat(
                device_, format, SDL_GPU_TEXTURETYPE_2D, usage)) {
            shadowColorFormat_ = format;
            shadowDepthPacked_ = format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            break;
        }
    }
    if (shadowColorFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID) {
        lastError_ = "GPU does not support a sampleable shadow color target";
        return false;
    }

    SDL_GPUTextureCreateInfo colorInfo{};
    colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
    colorInfo.format = shadowColorFormat_;
    colorInfo.usage = usage;
    colorInfo.width = shadowMapSize_;
    colorInfo.height = shadowMapSize_;
    colorInfo.layer_count_or_depth = 1;
    colorInfo.num_levels = 1;
    colorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    shadowColorTexture_ = SDL_CreateGPUTexture(device_, &colorInfo);

    SDL_GPUTextureCreateInfo depthInfo{};
    depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
    depthInfo.format = depthFormat_;
    depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depthInfo.width = shadowMapSize_;
    depthInfo.height = shadowMapSize_;
    depthInfo.layer_count_or_depth = 1;
    depthInfo.num_levels = 1;
    depthInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    shadowDepthTexture_ = SDL_CreateGPUTexture(device_, &depthInfo);
    if (shadowColorTexture_ == nullptr || shadowDepthTexture_ == nullptr) {
        setError("SDL_CreateGPUTexture(shadow)");
        return false;
    }

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadowSampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
    if (shadowSampler_ == nullptr) {
        setError("SDL_CreateGPUSampler(shadow)");
        return false;
    }

    shadowMappingEnabled_ = true;
    return true;
}

bool GpuRenderer::createDepthTexture(Uint32 width, Uint32 height) {
    if (depthTexture_ != nullptr && sceneColorTexture_ != nullptr &&
        ssrDiagnosticsTexture_ != nullptr && ssrDiagnosticsTransferBuffer_ != nullptr &&
        depthWidth_ == width && depthHeight_ == height &&
        (sampleCount_ == SDL_GPU_SAMPLECOUNT_1 || multisampleColorTexture_ != nullptr)) {
        return true;
    }
    if (ssrDiagnosticsFence_ != nullptr) {
        SDL_GPUFence* pendingFence = ssrDiagnosticsFence_;
        if (SDL_WaitForGPUFences(device_, true, &pendingFence, 1)) {
            processSsrDiagnosticsReadback();
        } else {
            const std::string waitError = SDL_GetError();
            ssrDiagnosticsError_ =
                "SDL_WaitForGPUFences(SSR diagnostics resize) failed: " + waitError;
            lastError_ = ssrDiagnosticsError_;
            return false;
        }
    }
    if (ssrDiagnosticsTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, ssrDiagnosticsTexture_);
        ssrDiagnosticsTexture_ = nullptr;
    }
    if (ssrDiagnosticsTransferBuffer_ != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, ssrDiagnosticsTransferBuffer_);
        ssrDiagnosticsTransferBuffer_ = nullptr;
    }
    if (depthTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, depthTexture_);
        depthTexture_ = nullptr;
    }
    if (ssaoDepthTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, ssaoDepthTexture_);
        ssaoDepthTexture_ = nullptr;
    }
    if (ssaoDepthAttachmentTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, ssaoDepthAttachmentTexture_);
        ssaoDepthAttachmentTexture_ = nullptr;
    }
    if (sceneColorTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, sceneColorTexture_);
        sceneColorTexture_ = nullptr;
    }
    if (multisampleColorTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, multisampleColorTexture_);
        multisampleColorTexture_ = nullptr;
    }

    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = depthFormat_;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    textureInfo.sample_count = sampleCount_;
    depthTexture_ = SDL_CreateGPUTexture(device_, &textureInfo);
    if (depthTexture_ == nullptr) {
        depthWidth_ = 0;
        depthHeight_ = 0;
        setError("SDL_CreateGPUTexture(depth)");
        return false;
    }

    SDL_GPUTextureCreateInfo sceneColorInfo{};
    sceneColorInfo.type = SDL_GPU_TEXTURETYPE_2D;
    sceneColorInfo.format = sceneColorFormat_;
    sceneColorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    sceneColorInfo.width = width;
    sceneColorInfo.height = height;
    sceneColorInfo.layer_count_or_depth = 1;
    sceneColorInfo.num_levels = 1;    sceneColorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    if (!SDL_GPUTextureSupportsFormat(device_, sceneColorInfo.format,
            SDL_GPU_TEXTURETYPE_2D, sceneColorInfo.usage)) {        SDL_ReleaseGPUTexture(device_, depthTexture_);
        depthTexture_ = nullptr;
        depthWidth_ = 0;
        depthHeight_ = 0;
        lastError_ = "GPU swapchain format cannot be used for sampled scene color";
        return false;
    }
    sceneColorTexture_ = SDL_CreateGPUTexture(device_, &sceneColorInfo);
    if (sceneColorTexture_ == nullptr) {
        SDL_ReleaseGPUTexture(device_, depthTexture_);
        depthTexture_ = nullptr;
        depthWidth_ = 0;
        depthHeight_ = 0;
        setError("SDL_CreateGPUTexture(scene color)");
        return false;
    }

    SDL_GPUTextureCreateInfo ssaoColorInfo{};
    ssaoColorInfo.type = SDL_GPU_TEXTURETYPE_2D;
    ssaoColorInfo.format = shadowColorFormat_;
    ssaoColorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ssaoColorInfo.width = width;
    ssaoColorInfo.height = height;
    ssaoColorInfo.layer_count_or_depth = 1;
    ssaoColorInfo.num_levels = 1;
    ssaoColorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ssaoDepthTexture_ = SDL_CreateGPUTexture(device_, &ssaoColorInfo);
    if (ssaoDepthTexture_ == nullptr) {
        setError("SDL_CreateGPUTexture(SSAO depth color)");
        return false;
    }

    SDL_GPUTextureCreateInfo ssaoAttachmentInfo{};
    ssaoAttachmentInfo.type = SDL_GPU_TEXTURETYPE_2D;
    ssaoAttachmentInfo.format = depthFormat_;
    ssaoAttachmentInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ssaoAttachmentInfo.width = width;
    ssaoAttachmentInfo.height = height;
    ssaoAttachmentInfo.layer_count_or_depth = 1;
    ssaoAttachmentInfo.num_levels = 1;
    ssaoAttachmentInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ssaoDepthAttachmentTexture_ = SDL_CreateGPUTexture(device_, &ssaoAttachmentInfo);
    if (ssaoDepthAttachmentTexture_ == nullptr) {
        setError("SDL_CreateGPUTexture(SSAO depth attachment)");
        return false;
    }

    const std::uint64_t diagnosticByteCount =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4U;
    if (diagnosticByteCount == 0 ||
        diagnosticByteCount > std::numeric_limits<Uint32>::max()) {
        lastError_ = "SSR diagnostics readback exceeds SDL_GPU transfer-buffer limits";
        return false;
    }
    SDL_GPUTextureCreateInfo diagnosticsInfo{};
    diagnosticsInfo.type = SDL_GPU_TEXTURETYPE_2D;
    diagnosticsInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    diagnosticsInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    diagnosticsInfo.width = width;
    diagnosticsInfo.height = height;
    diagnosticsInfo.layer_count_or_depth = 1;
    diagnosticsInfo.num_levels = 1;
    diagnosticsInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ssrDiagnosticsTexture_ = SDL_CreateGPUTexture(device_, &diagnosticsInfo);
    if (ssrDiagnosticsTexture_ == nullptr) {
        setError("SDL_CreateGPUTexture(SSR diagnostics)");
        return false;
    }
    SDL_GPUTransferBufferCreateInfo diagnosticsTransferInfo{};
    diagnosticsTransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    diagnosticsTransferInfo.size = static_cast<Uint32>(diagnosticByteCount);
    ssrDiagnosticsTransferBuffer_ =
        SDL_CreateGPUTransferBuffer(device_, &diagnosticsTransferInfo);
    if (ssrDiagnosticsTransferBuffer_ == nullptr) {
        setError("SDL_CreateGPUTransferBuffer(SSR diagnostics)");
        return false;
    }
    ssrDiagnosticsWidth_ = width;
    ssrDiagnosticsHeight_ = height;

    if (sampleCount_ != SDL_GPU_SAMPLECOUNT_1) {
        SDL_GPUTextureCreateInfo colorInfo{};
        colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
        colorInfo.format = sceneColorFormat_;
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        colorInfo.width = width;
        colorInfo.height = height;
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = 1;
        colorInfo.sample_count = sampleCount_;
        multisampleColorTexture_ = SDL_CreateGPUTexture(device_, &colorInfo);
        if (multisampleColorTexture_ == nullptr) {
            SDL_ReleaseGPUTexture(device_, sceneColorTexture_);
            sceneColorTexture_ = nullptr;
            SDL_ReleaseGPUTexture(device_, depthTexture_);
            depthTexture_ = nullptr;
            depthWidth_ = 0;
            depthHeight_ = 0;
            setError("SDL_CreateGPUTexture(multisample color)");
            return false;
        }
    }
    depthWidth_ = width;
    depthHeight_ = height;
    return true;
}

void GpuRenderer::processSsrDiagnosticsReadback() {
    if (device_ == nullptr || ssrDiagnosticsFence_ == nullptr ||
        !SDL_QueryGPUFence(device_, ssrDiagnosticsFence_)) {
        return;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, ssrDiagnosticsTransferBuffer_, false);
    if (mapped != nullptr) {
        const auto* pixels = static_cast<const Uint8*>(mapped);
        SsrGpuDiagnostics result{};
        result.sampleSerial = ssrGpuDiagnostics_.sampleSerial + 1U;
        result.totalPixels = static_cast<std::uint64_t>(ssrDiagnosticsWidth_) *
                             static_cast<std::uint64_t>(ssrDiagnosticsHeight_);
        for (std::uint64_t index = 0; index < result.totalPixels; ++index) {
            const Uint8* pixel = pixels + index * 4U;
            if (pixel[0] >= 128U) ++result.depthValidPixels;
            if (pixel[1] >= 128U) ++result.reflectivePixels;
            if (pixel[3] >= 128U) ++result.rayHitPixels;
            const auto steps = static_cast<std::uint64_t>(
                (static_cast<unsigned int>(pixel[2]) * 64U + 127U) / 255U);
            if (steps > 0U) {
                ++result.rayMarchPixels;
                result.rayMarchSteps += steps;
            }
        }
        result.averageRayMarchSteps = result.rayMarchPixels > 0U
            ? static_cast<float>(result.rayMarchSteps) /
              static_cast<float>(result.rayMarchPixels)
            : 0.0F;
        ssrGpuDiagnostics_ = result;
        SDL_UnmapGPUTransferBuffer(device_, ssrDiagnosticsTransferBuffer_);
    } else {
        ssrDiagnosticsError_ = std::string("SDL_MapGPUTransferBuffer(SSR diagnostics) failed: ") +
                               SDL_GetError();
    }

    SDL_ReleaseGPUFence(device_, ssrDiagnosticsFence_);
    ssrDiagnosticsFence_ = nullptr;
}

bool GpuRenderer::renderFrame(
    const Camera& camera, const std::vector<SceneRenderItem>& renderItems,
    bool showTouchControls, bool movementActive, float movementX, float movementY,
    float framesPerSecond, bool wireframe, bool ssrEnabled, Uint32 ssrDebugView) {
    processSsrDiagnosticsReadback();
    if (device_ == nullptr || window_ == nullptr || pipeline_ == nullptr ||
        wireframePipeline_ == nullptr || bloomPipeline_ == nullptr ||
        bloomSampler_ == nullptr ||
        vertexBuffer_ == nullptr || indexBuffer_ == nullptr || meshRanges_.empty()) {
        lastError_ = "GPU renderer is not initialized";
        return false;
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr) {
        setError("SDL_AcquireGPUCommandBuffer");
        return false;
    }

    SDL_GPUTexture* swapchainTexture = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            commandBuffer, window_, &swapchainTexture, &width, &height)) {
        setError("SDL_WaitAndAcquireGPUSwapchainTexture");
        (void)SDL_CancelGPUCommandBuffer(commandBuffer);
        return false;
    }
    if (swapchainTexture == nullptr || width == 0 || height == 0) {
        if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
            setError("SDL_SubmitGPUCommandBuffer");
            return false;
        }
        return true;
    }
    if (!createDepthTexture(width, height)) {
        (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
        return false;
    }

    const SDL_GPUBufferBinding vertexBinding{vertexBuffer_, 0};
    const SDL_GPUBufferBinding indexBinding{indexBuffer_, 0};
    const SDL_GPUTextureSamplerBinding colorBinding{colorTexture_, colorSampler_};
    const Matrix4 view = viewMatrix(camera);
    const Matrix4 projection = projectionMatrix(
        static_cast<float>(width) / static_cast<float>(height));
    const Matrix4 viewProjection = multiply(projection, view);

    // Render a single-sample camera-depth prepass for the SSAO composite.
    SDL_GPUColorTargetInfo ssaoColorTarget{};
    ssaoColorTarget.texture = ssaoDepthTexture_;
    ssaoColorTarget.clear_color = SDL_FColor{1.0F, 1.0F, 1.0F, 1.0F};
    ssaoColorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    ssaoColorTarget.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo ssaoDepthTarget{};
    ssaoDepthTarget.texture = ssaoDepthAttachmentTexture_;
    ssaoDepthTarget.clear_depth = 1.0F;
    ssaoDepthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    ssaoDepthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
    ssaoDepthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    ssaoDepthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* ssaoPass = SDL_BeginGPURenderPass(
        commandBuffer, &ssaoColorTarget, 1, &ssaoDepthTarget);
    if (ssaoPass == nullptr) {
        setError("SDL_BeginGPURenderPass(SSAO depth)");
        (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
        return false;
    }
    SDL_BindGPUGraphicsPipeline(ssaoPass, shadowPipeline_);
    SDL_BindGPUVertexBuffers(ssaoPass, 0, &vertexBinding, 1);
    SDL_BindGPUIndexBuffer(ssaoPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    for (const SceneRenderItem& item : renderItems) {
        const SceneObject* object = item.object;
        if (!item.renderOnGpu || object == nullptr || !object->visible ||
            item.gpuMeshIndex >= meshRanges_.size()) continue;
        const Matrix4 model = modelMatrix(object->transform);
        struct DepthConstants { Matrix4 mvp; Matrix4 model; float depthFlags[4]; } constants{};
        constants.model = model;
        constants.depthFlags[0] = shadowDepthPacked_ ? 1.0F : 0.0F;
        constants.mvp = multiply(viewProjection, model);
        SDL_PushGPUVertexUniformData(commandBuffer, 0, &constants,
            static_cast<Uint32>(sizeof(constants)));
        const MeshRange& range = meshRanges_[item.gpuMeshIndex];
        SDL_DrawGPUIndexedPrimitives(ssaoPass, range.indexCount, 1, range.firstIndex, 0, 0);
    }
    SDL_EndGPURenderPass(ssaoPass);

    const Matrix4 lightVP = shadowViewProjection();
    if (shadowMappingEnabled_) {
        SDL_GPUColorTargetInfo shadowColorTarget{};
        shadowColorTarget.texture = shadowColorTexture_;
        shadowColorTarget.clear_color = SDL_FColor{1.0F, 1.0F, 1.0F, 1.0F};
        shadowColorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        shadowColorTarget.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo shadowDepthTarget{};
        shadowDepthTarget.texture = shadowDepthTexture_;
        shadowDepthTarget.clear_depth = 1.0F;
        shadowDepthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        shadowDepthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
        shadowDepthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        shadowDepthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* shadowPass = SDL_BeginGPURenderPass(
            commandBuffer, &shadowColorTarget, 1, &shadowDepthTarget);
        if (shadowPass == nullptr) {
            setError("SDL_BeginGPURenderPass(shadow)");
            (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
            return false;
        }
        SDL_BindGPUGraphicsPipeline(shadowPass, shadowPipeline_);
        SDL_BindGPUVertexBuffers(shadowPass, 0, &vertexBinding, 1);
        SDL_BindGPUIndexBuffer(shadowPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        for (const SceneRenderItem& item : renderItems) {
            const SceneObject* object = item.object;
            if (!item.renderOnGpu || object == nullptr || !object->visible ||
                item.gpuMeshIndex >= meshRanges_.size()) continue;
            const Matrix4 model = modelMatrix(object->transform);
            struct ShadowConstants { Matrix4 mvp; Matrix4 model; float depthFlags[4]; } constants{};
            constants.model = model;
            constants.depthFlags[0] = shadowDepthPacked_ ? 1.0F : 0.0F;
            constants.mvp = multiply(lightVP, model);
            SDL_PushGPUVertexUniformData(commandBuffer, 0, &constants,
                static_cast<Uint32>(sizeof(constants)));
            const MeshRange& range = meshRanges_[item.gpuMeshIndex];
            SDL_DrawGPUIndexedPrimitives(shadowPass, range.indexCount, 1, range.firstIndex, 0, 0);
        }
        SDL_EndGPURenderPass(shadowPass);
    }

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = sampleCount_ == SDL_GPU_SAMPLECOUNT_1
        ? sceneColorTexture_ : multisampleColorTexture_;
    colorTarget.clear_color = SDL_FColor{0.02F, 0.11F, 0.19F, 1.0F};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = sampleCount_ == SDL_GPU_SAMPLECOUNT_1
        ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_RESOLVE;
    colorTarget.resolve_texture = sampleCount_ == SDL_GPU_SAMPLECOUNT_1
        ? nullptr : sceneColorTexture_;
    SDL_GPUDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = depthTexture_;
    depthTarget.clear_depth = 1.0F;
    depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, &depthTarget);
    if (pass == nullptr) {
        setError("SDL_BeginGPURenderPass(scene)");
        (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
        return false;
    }
    SDL_BindGPUGraphicsPipeline(pass, wireframe ? wireframePipeline_ : pipeline_);
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_BindGPUFragmentSamplers(pass, 0, &colorBinding, 1);
    if (shadowMappingEnabled_) {
        const SDL_GPUTextureSamplerBinding shadowBinding{shadowColorTexture_, shadowSampler_};
        SDL_BindGPUFragmentSamplers(pass, 1, &shadowBinding, 1);
    }

    Vec3 pointLightPosition{0.0F, 0.0F, 4.2F};
    for (const SceneRenderItem& instance : renderItems) {
        if (instance.renderOnGpu && instance.object != nullptr && instance.gpuMeshIndex == 1U) {
            pointLightPosition = instance.object->transform.position;
            break;
        }
    }

    SDL_BindGPUGraphicsPipeline(pass, wireframe ? wireframePipeline_ : pipeline_);
    for (const SceneRenderItem& instance : renderItems) {
        const SceneObject* object = instance.object;
        if (!instance.renderOnGpu || object == nullptr || !object->visible || instance.gpuMeshIndex >= meshRanges_.size()) {
            continue;
        }
        const Matrix4 model = modelMatrix(object->transform);
        ObjectConstants constants{};
        constants.model = model;
        constants.mvp = multiply(viewProjection, model);
        constants.normalMatrix = normalMatrix(object->transform);
        constants.cameraPosition[0] = camera.x();
        constants.cameraPosition[1] = camera.y();
        constants.cameraPosition[2] = camera.z();
        constants.cameraPosition[3] = 1.0F;
        constants.materialFlags[0] = instance.gpuMeshIndex == 2U ? 1.0F : 0.0F;
        constants.materialFlags[1] = instance.gpuMeshIndex == 1U ? 1.0F : 0.0F;
        constants.materialFlags[2] = std::clamp(instance.metallic, 0.0F, 1.0F);
        constants.materialFlags[3] = std::clamp(instance.roughness, 0.045F, 1.0F);
        constants.pointLightPositionRange[0] = pointLightPosition.x;
        constants.pointLightPositionRange[1] = pointLightPosition.y;
        constants.pointLightPositionRange[2] = pointLightPosition.z;
        constants.pointLightPositionRange[3] = 8.0F;
        constants.pointLightColorIntensity[0] = 0.02F;
        constants.pointLightColorIntensity[1] = 0.72F;
        constants.pointLightColorIntensity[2] = 1.0F;
        constants.pointLightColorIntensity[3] = 3.2F;
        constants.shadowMvp = multiply(lightVP, model);
        constants.depthFlags[0] = shadowDepthPacked_ ? 1.0F : 0.0F;
        SDL_PushGPUVertexUniformData(commandBuffer, 0, &constants,
                                     static_cast<Uint32>(sizeof(constants)));
        const MeshRange& range = meshRanges_[instance.gpuMeshIndex];
        SDL_DrawGPUIndexedPrimitives(pass, range.indexCount, 1, range.firstIndex, 0, 0);
    }

    SDL_EndGPURenderPass(pass);

    SDL_GPUColorTargetInfo compositeTargets[2]{};
    compositeTargets[0].texture = swapchainTexture;
    compositeTargets[0].clear_color = SDL_FColor{0.02F, 0.11F, 0.19F, 1.0F};
    compositeTargets[0].load_op = SDL_GPU_LOADOP_CLEAR;
    compositeTargets[0].store_op = SDL_GPU_STOREOP_STORE;
    compositeTargets[1].texture = ssrDiagnosticsTexture_;
    compositeTargets[1].clear_color = SDL_FColor{0.0F, 0.0F, 0.0F, 0.0F};
    compositeTargets[1].load_op = SDL_GPU_LOADOP_CLEAR;
    compositeTargets[1].store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* compositePass = SDL_BeginGPURenderPass(
        commandBuffer, compositeTargets, 2, nullptr);
    if (compositePass == nullptr) {
        setError("SDL_BeginGPURenderPass(bloom composite)");
        (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
        return false;
    }
    SDL_BindGPUGraphicsPipeline(compositePass, bloomPipeline_);
    const SDL_GPUTextureSamplerBinding sceneBinding{sceneColorTexture_, bloomSampler_};
    const SDL_GPUTextureSamplerBinding ssaoBinding{ssaoDepthTexture_, shadowSampler_};
    SDL_BindGPUFragmentSamplers(compositePass, 0, &sceneBinding, 1);
    SDL_BindGPUFragmentSamplers(compositePass, 1, &ssaoBinding, 1);
    CompositeConstants compositeConstants{};
    compositeConstants.viewProjection = viewProjection;
    compositeConstants.inverseViewProjection = inverseMatrix(viewProjection);
    compositeConstants.cameraPosition[0] = camera.x();
    compositeConstants.cameraPosition[1] = camera.y();
    compositeConstants.cameraPosition[2] = camera.z();
    compositeConstants.cameraPosition[3] = 1.0F;
    compositeConstants.depthFlags[0] = shadowDepthPacked_ ? 1.0F : 0.0F;
    compositeConstants.depthFlags[1] = ssrEnabled ? 1.0F : 0.0F;
    compositeConstants.depthFlags[2] = static_cast<float>(ssrDebugView);
    SDL_PushGPUFragmentUniformData(commandBuffer, 0, &compositeConstants,
        static_cast<Uint32>(sizeof(compositeConstants)));
    SDL_DrawGPUPrimitives(compositePass, 3, 1, 0, 0);
    if (showTouchControls && overlayPipeline_ != nullptr) {
        drawOverlay(commandBuffer, compositePass, width, height, movementActive, movementX, movementY,
                    framesPerSecond, ssrEnabled, ssrDebugView);
    }
    SDL_EndGPURenderPass(compositePass);

    const Uint64 diagnosticTicks = SDL_GetTicks();
    const bool requestDiagnostics =
        ssrDiagnosticsFence_ == nullptr &&
        diagnosticTicks - lastSsrDiagnosticsReadbackTicks_ >= 1000U;
    if (requestDiagnostics) {
        SDL_GPUCopyPass* diagnosticsCopyPass = SDL_BeginGPUCopyPass(commandBuffer);
        if (diagnosticsCopyPass != nullptr) {
            SDL_GPUTextureRegion sourceRegion{};
            sourceRegion.texture = ssrDiagnosticsTexture_;
            sourceRegion.w = ssrDiagnosticsWidth_;
            sourceRegion.h = ssrDiagnosticsHeight_;
            sourceRegion.d = 1;
            SDL_GPUTextureTransferInfo destinationInfo{};
            destinationInfo.transfer_buffer = ssrDiagnosticsTransferBuffer_;
            destinationInfo.pixels_per_row = ssrDiagnosticsWidth_;
            destinationInfo.rows_per_layer = ssrDiagnosticsHeight_;
            SDL_DownloadFromGPUTexture(diagnosticsCopyPass, &sourceRegion, &destinationInfo);
            SDL_EndGPUCopyPass(diagnosticsCopyPass);
            ssrDiagnosticsFence_ =
                SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
            if (ssrDiagnosticsFence_ == nullptr) {
                setError("SDL_SubmitGPUCommandBufferAndAcquireFence(SSR diagnostics)");
                return false;
            }
            lastSsrDiagnosticsReadbackTicks_ = diagnosticTicks;
            return true;
        }
        ssrDiagnosticsError_ = std::string("SDL_BeginGPUCopyPass(SSR diagnostics) failed: ") +
                               SDL_GetError();
    }

    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        setError("SDL_SubmitGPUCommandBuffer");
        return false;
    }
    return true;
}


void GpuRenderer::drawOverlayRect(
    SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
    float x, float y, float rectWidth, float rectHeight,
    Uint32 viewportWidth, Uint32 viewportHeight, const float color[4]) {
    OverlayConstants constants{};
    constants.center[0] = x + rectWidth * 0.5F;
    constants.center[1] = y + rectHeight * 0.5F;
    constants.radius[0] = rectWidth * 0.5F;
    constants.radius[1] = rectHeight * 0.5F;
    std::copy_n(color, 4, constants.color);
    constants.viewport[0] = static_cast<float>(viewportWidth);
    constants.viewport[1] = static_cast<float>(viewportHeight);
    constants.shape = 1;
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &constants,
                                 static_cast<Uint32>(sizeof(constants)));
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
}

void GpuRenderer::drawOverlayCircle(
    SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
    float x, float y, float radiusX, float radiusY,
    Uint32 viewportWidth, Uint32 viewportHeight, const float color[4]) {
    OverlayConstants constants{};
    constants.center[0] = x;
    constants.center[1] = y;
    constants.radius[0] = radiusX;
    constants.radius[1] = radiusY;
    std::copy_n(color, 4, constants.color);
    constants.viewport[0] = static_cast<float>(viewportWidth);
    constants.viewport[1] = static_cast<float>(viewportHeight);
    constants.shape = 0;
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &constants,
                                 static_cast<Uint32>(sizeof(constants)));
    SDL_DrawGPUPrimitives(pass, 32U * 6U, 1, 0, 0);
}

void GpuRenderer::drawOverlayText(
    SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
    float x, float y, const char* text, Uint32 viewportWidth,
    Uint32 viewportHeight, const float color[4]) {
    using namespace touch_overlay;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const std::string_view pattern = glyph(*cursor);
        int row = 0;
        int column = 0;
        for (const char pixel : pattern) {
            if (pixel == '/') { ++row; column = 0; continue; }
            if (pixel == '1') {
                drawOverlayRect(commandBuffer, pass,
                    x + static_cast<float>(column) * fontPixelSize,
                    y + static_cast<float>(row) * fontPixelSize,
                    fontPixelSize, fontPixelSize, viewportWidth, viewportHeight, color);
            }
            ++column;
        }
        x += fontAdvance;
    }
}

void GpuRenderer::drawOverlay(
    SDL_GPUCommandBuffer* commandBuffer, SDL_GPURenderPass* pass,
    Uint32 width, Uint32 height, bool movementActive, float movementX, float movementY,
    float framesPerSecond, bool ssrEnabled, Uint32 ssrDebugView) {
    SDL_BindGPUGraphicsPipeline(pass, overlayPipeline_);
    using namespace touch_overlay;
    const float dimension = static_cast<float>(std::min(width, height));
    const float joystickX = static_cast<float>(width) * movementCenterX;
    const float joystickY = static_cast<float>(height) * controlCenterY;
    const float joystickRadius = dimension * movementRadius;
    const float lookX = static_cast<float>(width) * lookCenterX;
    const float lookY = static_cast<float>(height) * controlCenterY;
    const float lookRadiusValue = dimension * lookRadius;
    const auto ring = normalized(ringColor);
    const auto guide = normalized(guideColor);
    const auto knob = normalized(knobColor);
    const auto button = normalized(buttonColor);
    const auto ssrButton = normalized(ssrEnabled ? buttonColor : ssrDisabledButtonColor);
    const auto debugButton = normalized(ssrDebugView == 0 ? ssrDisabledButtonColor : buttonColor);
    const auto text = normalized(textColor);

    drawOverlayCircle(commandBuffer, pass, joystickX, joystickY,
                      joystickRadius, joystickRadius, width, height, ring.data());
    drawOverlayCircle(commandBuffer, pass, joystickX, joystickY,
                      joystickRadius * movementInnerRadius, joystickRadius * movementInnerRadius,
                      width, height, ring.data());
    drawOverlayCircle(commandBuffer, pass, joystickX, joystickY,
                      joystickRadius * knobRadius, joystickRadius * knobRadius,
                      width, height, ring.data());
    drawOverlayRect(commandBuffer, pass, joystickX - joystickRadius * movementGuideRadius,
                    joystickY - 0.5F, joystickRadius * movementGuideRadius * 2.0F, 1.0F,
                    width, height, guide.data());
    drawOverlayRect(commandBuffer, pass, joystickX - 0.5F,
                    joystickY - joystickRadius * movementGuideRadius, 1.0F,
                    joystickRadius * movementGuideRadius * 2.0F, width, height, guide.data());
    const float knobX = movementActive
        ? joystickX + std::clamp(movementX * static_cast<float>(width) - joystickX,
                                 -joystickRadius * movementGuideRadius, joystickRadius * movementGuideRadius)
        : joystickX;
    const float knobY = movementActive
        ? joystickY + std::clamp(movementY * static_cast<float>(height) - joystickY,
                                 -joystickRadius * movementGuideRadius, joystickRadius * movementGuideRadius)
        : joystickY;
    drawOverlayCircle(commandBuffer, pass, knobX, knobY,
                      joystickRadius * knobRadius, joystickRadius * knobRadius,
                      width, height, knob.data());

    drawOverlayCircle(commandBuffer, pass, lookX, lookY, lookRadiusValue, lookRadiusValue,
                      width, height, ring.data());
    drawOverlayCircle(commandBuffer, pass, lookX, lookY,
                      lookRadiusValue * lookInnerRadius, lookRadiusValue * lookInnerRadius,
                      width, height, ring.data());
    drawOverlayRect(commandBuffer, pass, lookX - lookRadiusValue * lookGuideRadius,
                    lookY - 0.5F, lookRadiusValue * lookGuideRadius * 2.0F, 1.0F,
                    width, height, guide.data());
    drawOverlayRect(commandBuffer, pass, lookX - 0.5F,
                    lookY - lookRadiusValue * lookGuideRadius, 1.0F,
                    lookRadiusValue * lookGuideRadius * 2.0F, width, height, guide.data());

    char fpsText[24]{};
    std::snprintf(fpsText, sizeof(fpsText), "FPS:%d",
                  std::max(0, static_cast<int>(std::round(framesPerSecond))));
    drawOverlayText(commandBuffer, pass, fpsX, fpsY, fpsText, width, height, text.data());

    const float buttonX = static_cast<float>(width) - backendButtonRight - backendButtonWidth;
    const float ssrButtonX = static_cast<float>(width) - ssrButtonRight - ssrButtonWidth;
    const float debugButtonX = static_cast<float>(width) - ssrDebugButtonRight - ssrDebugButtonWidth;
    drawOverlayRect(commandBuffer, pass, debugButtonX, backendButtonY,
                    ssrDebugButtonWidth, backendButtonHeight, width, height, debugButton.data());
    drawOverlayText(commandBuffer, pass, debugButtonX + 17.0F,
                    backendButtonY + (backendButtonHeight - fontHeight) * 0.5F,
                    "DBG", width, height, text.data());

    drawOverlayRect(commandBuffer, pass, ssrButtonX, backendButtonY,
                    ssrButtonWidth, backendButtonHeight, width, height, ssrButton.data());
    drawOverlayText(commandBuffer, pass, ssrButtonX + 18.0F,
                    backendButtonY + (backendButtonHeight - fontHeight) * 0.5F,
                    "SSR", width, height, text.data());

    drawOverlayRect(commandBuffer, pass, buttonX, backendButtonY,
                    backendButtonWidth, backendButtonHeight, width, height, button.data());
    drawOverlayText(commandBuffer, pass, buttonX + 33.0F,
                    backendButtonY + (backendButtonHeight - fontHeight) * 0.5F,
                    "CPU", width, height, text.data());
}

void GpuRenderer::shutdown() noexcept {
    if (device_ != nullptr) {
        if (ssrDiagnosticsFence_ != nullptr) {
            SDL_GPUFence* pendingFence = ssrDiagnosticsFence_;
            (void)SDL_WaitForGPUFences(device_, true, &pendingFence, 1);
            SDL_ReleaseGPUFence(device_, ssrDiagnosticsFence_);
            ssrDiagnosticsFence_ = nullptr;
        }
        if (ssrDiagnosticsTransferBuffer_ != nullptr) {
            SDL_ReleaseGPUTransferBuffer(device_, ssrDiagnosticsTransferBuffer_);
            ssrDiagnosticsTransferBuffer_ = nullptr;
        }
        if (ssrDiagnosticsTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, ssrDiagnosticsTexture_);
            ssrDiagnosticsTexture_ = nullptr;
        }
        ssrDiagnosticsWidth_ = 0;
        ssrDiagnosticsHeight_ = 0;
        if(shadowSampler_){SDL_ReleaseGPUSampler(device_,shadowSampler_);shadowSampler_=nullptr;}
        if(ssaoDepthTexture_){SDL_ReleaseGPUTexture(device_,ssaoDepthTexture_);ssaoDepthTexture_=nullptr;}
        if(ssaoDepthAttachmentTexture_){SDL_ReleaseGPUTexture(device_,ssaoDepthAttachmentTexture_);ssaoDepthAttachmentTexture_=nullptr;}
        if(shadowColorTexture_){SDL_ReleaseGPUTexture(device_,shadowColorTexture_);shadowColorTexture_=nullptr;}
        if(shadowDepthTexture_){SDL_ReleaseGPUTexture(device_,shadowDepthTexture_);shadowDepthTexture_=nullptr;}
        if(shadowPipeline_){SDL_ReleaseGPUGraphicsPipeline(device_,shadowPipeline_);shadowPipeline_=nullptr;}
        if (sceneColorTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, sceneColorTexture_);
            sceneColorTexture_ = nullptr;
        }
        if (depthTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, depthTexture_);
            depthTexture_ = nullptr;
        }
        if (multisampleColorTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, multisampleColorTexture_);
            multisampleColorTexture_ = nullptr;
        }
        if (bloomSampler_ != nullptr) {
            SDL_ReleaseGPUSampler(device_, bloomSampler_);
            bloomSampler_ = nullptr;
        }
        if (colorSampler_ != nullptr) {
            SDL_ReleaseGPUSampler(device_, colorSampler_);
            colorSampler_ = nullptr;
        }
        if (colorTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, colorTexture_);
            colorTexture_ = nullptr;
        }
        depthWidth_ = 0;
        depthHeight_ = 0;
        if (bloomPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, bloomPipeline_);
            bloomPipeline_ = nullptr;
        }
        if (overlayPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, overlayPipeline_);
            overlayPipeline_ = nullptr;
        }
        if (wireframePipeline_ != nullptr) {            SDL_ReleaseGPUGraphicsPipeline(device_, wireframePipeline_);
            wireframePipeline_ = nullptr;
        }
        if (pipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
            pipeline_ = nullptr;        }
        if (vertexBuffer_ != nullptr) {
            SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
            vertexBuffer_ = nullptr;
        }
        if (indexBuffer_ != nullptr) {
            SDL_ReleaseGPUBuffer(device_, indexBuffer_);
            indexBuffer_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
            window_ = nullptr;
        }
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
    }
    depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    sceneColorFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    sampleCount_ = SDL_GPU_SAMPLECOUNT_1;
    shadowMappingEnabled_ = false;
    shadowDepthPacked_ = false;
    shadowColorFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    ssrDiagnosticsWidth_ = 0;
    ssrDiagnosticsHeight_ = 0;
    ssrDiagnosticsError_.clear();
    meshRanges_.clear();
}

} // namespace originalengine