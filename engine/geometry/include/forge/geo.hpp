#pragma once

// ─── forge::geo — geometry kernel umbrella header ────────────────────────────
//
// Include this single header to get the complete geometry kernel:
//
//   #include <forge/geo.hpp>
//
//   forge::geo::Plane, Brush, BrushFace, BrushValidation, AABB, Side
//   forge::geo::classifyPoint, intersectThreePlanes, intersectSegment
//   forge::geo::computeBrushVertices, computeFacePolygon, clipPolygonByPlane
//   forge::geo::splitBrushByPlane, csgSubtract, hollowBrush
//   forge::geo::makeBox, makeWedge, makePrism, makePyramid

#include "forge/geo/Math.hpp"
#include "forge/geo/Plane.hpp"
#include "forge/geo/Brush.hpp"
#include "forge/geo/CSG.hpp"
#include "forge/geo/Primitives.hpp"
