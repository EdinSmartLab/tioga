#include "dMeshBlock.h"
#include "funcs.hpp"

/* --- Handy Vector Operation Macros --- */

#define CROSS(a, b, c) { \
  c[0] = a[1]*b[2] - a[2]*b[1]; \
  c[1] = a[2]*b[0] - a[0]*b[2]; \
  c[2] = a[0]*b[1] - a[1]*b[0]; }

#define CROSS4(a1, a2, b1, b2, c) { \
  c[0] = (a1[1]-a2[1])*(b1[2]-b2[2]) - (a1[2]-a2[2])*(b1[1]-b2[1]); \
  c[1] = (a1[2]-a2[2])*(b1[0]-b2[0]) - (a1[0]-a2[0])*(b1[2]-b2[2]); \
  c[2] = (a1[0]-a2[0])*(b1[1]-b2[1]) - (a1[1]-a2[1])*(b1[0]-b2[0]); }

#define DOT(a, b) (a[0]*b[0] + a[1]*b[1] + a[2]*b[2])

#define NORM(a) sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2])

static
__device__ __forceinline__
double DOTCROSS4(const double* __restrict__ c,
                 const double* __restrict__ a1, const double* __restrict__ a2,
                 const double* __restrict__ b1, const double* __restrict__ b2)
{
  double d[3];
  CROSS4(a1,a2,b1,b2,d)
  return DOT(c,d);
}

/* ------ dMeshBlock Member Functions ------ */

void dMeshBlock::dataToDevice(int ndims, int nnodes, int ncells, int ncells_adt,
    int nsearch, int* nv, int* nc, int* eleList, double* eleBBox, int* isearch,
    double* xsearch)
{
  this->nnodes = nnodes;
  this->ncells = ncells;
  this->nc_adt = ncells_adt;

  this->nv = nv;
  this->nc = nc;

  nvert = nv[0];

  this->eleBBox.assign(eleBBox, ncells_adt*ndims*2);
  this->eleList.assign(eleList, ncells_adt);

  this->nsearch = nsearch;
  this->isearch.assign(isearch, nsearch);
  this->xsearch.assign(xsearch, nsearch*nDims);
  rst.resize(nsearch*nDims);
  donorId.resize(nsearch);

  auto ijk2gmsh_h = tg_funcs::structured_to_gmsh_hex(nvert);
  ijk2gmsh.assign(ijk2gmsh_h.data(), ijk2gmsh_h.size());

  int nSide = std::cbrt(nvert);
  std::vector<double> xlist_h(nSide);
  double dxi = 2./(nSide-1);

  for (int i = 0; i < nSide; i++)
    xlist_h[i] = -1. + i*dxi;

  xlist.assign(xlist_h.data(), xlist_h.size());
}

void dMeshBlock::extraDataToDevice(int* vconn)
{
//  c2v.assign(vconn, nvert*ncells);
}

void dMeshBlock::updateSearchPoints(int nsearch, int *isearch, double *xsearch)
{
  this->nsearch = nsearch;
  this->isearch.assign(isearch, nsearch);
  this->xsearch.assign(xsearch, nsearch*nDims);
  rst.resize(nsearch*nDims);
  donorId.resize(nsearch);
}

void dMeshBlock::setDeviceData(double* vx, double* ex, int* ibc, int* ibf)
{
  x = vx;
  iblank_cell = ibc;
  iblank_face = ibf;
  coord = ex;
}

void dMeshBlock::setTransform(double* mat, double* off, int ndim)
{
  if (ndim != nDims)
    ThrowException("dMeshBlock::set_transform: input ndim != nDims");

  rrot = true;
  Rmat.assign(mat, ndim*ndim);
  offset.assign(off, ndim);
}

/* ---------------------------- Direct Cut Method Functions --------------------------- */

static
__device__
double lineSegmentDistance(double *p1, double *p2, double *p3, double *p4, double *dx)
{
  // Get the line equations
  double U[3] = {p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2]};
  double V[3] = {p4[0]-p3[0], p4[1]-p3[1], p4[2]-p3[2]};
  double W[3] = {p1[0]-p3[0], p1[1]-p3[1], p1[2]-p3[2]};
  double uu = U[0]*U[0] + U[1]*U[1] + U[2]*U[2];
  double vv = V[0]*V[0] + V[1]*V[1] + V[2]*V[2];
  double uv = U[0]*V[0] + U[1]*V[1] + U[2]*V[2];

  double uw = U[0]*W[0] + U[1]*W[1] + U[2]*W[2];
  double vw = V[0]*W[0] + V[1]*W[1] + V[2]*W[2];

  double den = uu*vv - uv*uv;

  // NOTE: not finding exact minimum distance between the line segments in all
  // cases; plenty close enough for our purposes
  // (see http://geomalgorithms.com/a07-_distance.html for full algo)

  // Calculate line parameters (if nearly parallel, set one & calculate other)
  double s = (den < 1e-10) ? 0 : (uv*vw - vv*uw) / den;
  double t = (den < 1e-10) ? uw / uv: (uu*vw - uv*uw) / den;

  s = fmin(fmax(s, 0.), 1.);
  t = fmin(fmax(t, 0.), 1.);

  // vec = closest distance from segment 1 to segment 2
  for (int i = 0; i < 3; i++)
    dx[i] = (p3[i] + t*V[i]) - (p1[i] + s*U[i]);

  double dist = 0;
  for (int i = 0; i < 3; i++)
    dist += dx[i]*dx[i];

  return sqrt(dist);
}

/*! Modified Moller triangle-triangle intersection algorithm
 *  Determines if triangles intersect, or returns an approximate minimum
 *  distance between them otherwise */
static
__device__
double triTriDistance(double* T1, double* T2, double* minVec, double tol)
{
  double dist = 1e15;
  double vec[3];
  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      int i2 = (i+1) % 3;
      int j2 = (j+1) % 3;
      double D = lineSegmentDistance(&T1[3*i], &T1[3*i2], &T2[3*j], &T2[3*j2], vec);

      if (D < dist)
      {
        for (int d = 0; d < 3; d++)
          minVec[d] = vec[d];
        dist = D;
      }
    }
  }

  if (dist < tol) return 0.;

  /// TODO: optimize (no use of point class)
  // Compute pi2 - N2 * X + d2 = 0
  dPoint V01 = dPoint(T1);
  dPoint V11 = dPoint(T1+3);
  dPoint V21 = dPoint(T1+6);

  dPoint V02 = dPoint(T2);
  dPoint V12 = dPoint(T2+3);
  dPoint V22 = dPoint(T2+6);

  dPoint N2 = (V12-V02).cross(V22-V02); // Plane 2
  N2 /= N2.norm();
  double d2 = -(N2*V02);

  dPoint N1 = (V11-V01).cross(V21-V01); // Plane 1
  N1 /= N1.norm();
  double d1 = -(N1*V01);

  // Signed distances of T1 points to T2's plane
  double d01 = N2*V01 + d2;
  double d11 = N2*V11 + d2;
  double d21 = N2*V21 + d2;

  // Signed distances of T2 points to T1's plane
  double d02 = N1*V02 + d1;
  double d12 = N1*V12 + d1;
  double d22 = N1*V22 + d1;

  // Round values near 0 to 0
  d01 = (fabs(d01) < 1e-10) ? 0 : d01;
  d11 = (fabs(d11) < 1e-10) ? 0 : d11;
  d21 = (fabs(d21) < 1e-10) ? 0 : d21;

  d02 = (fabs(d02) < 1e-10) ? 0 : d02;
  d12 = (fabs(d12) < 1e-10) ? 0 : d12;
  d22 = (fabs(d22) < 1e-10) ? 0 : d22;

  if (fabs(d01) + fabs(d11) + fabs(d21) < 3*tol ||
      fabs(d02) + fabs(d12) + fabs(d22) < 3*tol)
  {
    /* Approximately coplanar; check if one triangle is inside the other */

    // Check if a point in T1 is inside T2
    bool inside = true;
    inside = inside && N2*((V12-V02).cross(V01-V02)) > 0;
    inside = inside && N2*((V02-V22).cross(V01-V22)) > 0;
    inside = inside && N2*((V22-V12).cross(V01-V12)) > 0;

    if (inside) return 0.;

    // Check if a point in T2 is inside T1
    inside = true;
    inside = inside && N1*((V11-V01).cross(V02-V01)) > 0;
    inside = inside && N1*((V01-V21).cross(V02-V21)) > 0;
    inside = inside && N1*((V21-V11).cross(V02-V11)) > 0;

    if (inside) return 0.;
  }

  bool noTouch = false;

  // Check for intersection with plane - one point should have opposite sign
  if (sgn(d01) == sgn(d11) && sgn(d01) == sgn(d21)) // && fabs(d01) > tol)
  {
    noTouch = true;

    // No intersection; check if projection of points provides closer distance
    if (fabs(d01) < dist)
    {
      dPoint P01 = V01 - N2*d01;
      bool inside = true;
      inside = inside && N2*((V12-V02).cross(P01-V02)) > 0;
      inside = inside && N2*((V02-V22).cross(P01-V22)) > 0;
      inside = inside && N2*((V22-V12).cross(P01-V12)) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N2[i]*(d01);
        dist = fabs(d01);
      }
    }

    if (fabs(d11) < dist)
    {
      dPoint P11 = V11 - N2*d11;
      bool inside = true;
      inside = inside && N2*((V12-V02).cross(P11-V02)) > 0;
      inside = inside && N2*((V02-V22).cross(P11-V22)) > 0;
      inside = inside && N2*((V22-V12).cross(P11-V12)) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N2[i]*(d11);
        dist = fabs(d11);
      }
    }

    if (fabs(d21) < dist)
    {
      dPoint P21 = V21 - N2*d21;
      bool inside = true;
      inside = inside && N2*((V12-V02).cross(P21-V02)) > 0;
      inside = inside && N2*((V02-V22).cross(P21-V22)) > 0;
      inside = inside && N2*((V22-V12).cross(P21-V12)) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N2[i]*(d21);
        dist = fabs(d21);
      }
    }
  }

  // Check for intersection with plane - one point should have opposite sign
  if (sgn(d02) == sgn(d12) && sgn(d02) == sgn(d22)) // && fabs(d02) > tol)
  {
    noTouch = true;

    // No intersection; check if projection of points provides closer distance
    if (fabs(d02) < dist)
    {
      dPoint P02 = V02 - N1*d02;
      bool inside = true;
      inside = inside && N1*((V11-V01).cross(P02-V01)) > 0;
      inside = inside && N1*((V01-V21).cross(P02-V21)) > 0;
      inside = inside && N1*((V21-V11).cross(P02-V11)) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N1[i]*(d02);
        dist = fabs(d02);
      }
    }

    if (fabs(d12) < dist)
    {
      dPoint P12 = V12 - N1*d12;
      bool inside = true;
      inside = inside && N1*((V11-V01).cross(P12-V01)) > 0;
      inside = inside && N1*((V01-V21).cross(P12-V21)) > 0;
      inside = inside && N1*((V21-V11).cross(P12-V11)) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N1[i]*(d12);
        dist = fabs(d12);
      }
    }

    if (fabs(d22) < dist)
    {
      dPoint P22 = V22 - N1*d22;
      bool inside = true;
      inside = inside && N1*((V11-V01).cross(P22-V01)) > 0;
      inside = inside && N1*((V01-V21).cross(P22-V21)) > 0;
      inside = inside && N1*((V21-V11).cross(P22-V11)) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N1[i]*(d22);
        dist = fabs(d22);
      }
    }
  }

  // No intersection; return result from edge intersections & plane projections
  if (noTouch)
    return dist;

  // Compute intersection line
  dPoint L = N1.cross(N2);
  L /= L.norm();

  double p0 = L*V01;
  double p1 = L*V11;
  double p2 = L*V21;

  double q0 = L*V02;
  double q1 = L*V12;
  double q2 = L*V22;

  // Figure out which point of each triangle is opposite the other two
  int npt1 = (sgn(d01) != sgn(d11)) ? ( (sgn(d11) == sgn(d21)) ? 0 : 1 ) : 2;
  int npt2 = (sgn(d02) != sgn(d12)) ? ( (sgn(d12) == sgn(d22)) ? 0 : 1 ) : 2;

  double s1, s2;
  switch (npt1)
  {
    case 0:
      s1 = p1 + (p0-p1) * (d11 / (d11-d01));
      s2 = p2 + (p0-p2) * (d21 / (d21-d01));
      break;
    case 1:
      s1 = p0 + (p1-p0) * (d01 / (d01-d11));
      s2 = p2 + (p1-p2) * (d21 / (d21-d11));
      break;
    case 2:
      s1 = p0 + (p2-p0) * (d01 / (d01-d21));
      s2 = p1 + (p2-p1) * (d11 / (d11-d21));
      break;
  }

  double t1, t2;
  switch (npt2)
  {
    case 0:
      t1 = q1 + (q0-q1) * (d12 / (d12-d02));
      t2 = q2 + (q0-q2) * (d22 / (d22-d02));
      break;
    case 1:
      t1 = q0 + (q1-q0) * (d02 / (d02-d12));
      t2 = q2 + (q1-q2) * (d22 / (d22-d12));
      break;
    case 2:
      t1 = q0 + (q2-q0) * (d02 / (d02-d22));
      t2 = q1 + (q2-q1) * (d12 / (d12-d22));
      break;
  }

  s1 = (fabs(s1) < 1e-10) ? 0 : s1;
  s2 = (fabs(s2) < 1e-10) ? 0 : s2;
  t1 = (fabs(t1) < 1e-10) ? 0 : t1;
  t2 = (fabs(t2) < 1e-10) ? 0 : t2;

  if (s1 > s2)
    swap(s1,s2);

  if (t1 > t2)
    swap(t1,t2);

  if (s2 < t1 || t2 < s1)
  {
    // No overlap; return min of dt*L and minDist
    double dt = fmin(fabs(t1-s2), fabs(s1-t2));
    double dl = (L*dt).norm();

    if (dl < dist)
    {
      dist = dl;
      for (int i = 0; i < 3; i++)
        minVec[i] = sgn(t1-s2)*dt*L[i]; // Ensure vec is T1 -> T2
    }

    return dist;
  }

  return 0.;
}

static
__device__
double triTriDistance2(double* T1, double* T2, double* minVec, double tol)
{
  double dist = 1e15;
  double vec[3];
  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      int i2 = (i+1) % 3;
      int j2 = (j+1) % 3;
      double D = lineSegmentDistance(&T1[3*i], &T1[3*i2], &T2[3*j], &T2[3*j2], vec);

      if (D < dist)
      {
        for (int d = 0; d < 3; d++)
          minVec[d] = vec[d];
        dist = D;
      }
    }
  }

  // Pointers to points
  const double* V01 = T1;
  const double* V11 = T1+3;
  const double* V21 = T1+6;

  const double* V02 = T2;
  const double* V12 = T2+3;
  const double* V22 = T2+6;

  double N1[3], N2[3];

  // Plane for Triangle 1
  CROSS4(V11,V01, V21,V01, N1);

  double norm = NORM(N1);

  // Plane for Triangle 2
  for (int d = 0; d < 3; d++)
    N1[d] /= norm;

  double d1 = -DOT(N1,V01);

  CROSS4(V12,V02, V22,V02, N2);

  norm = NORM(N2);

  for (int d = 0; d < 3; d++)
    N2[d] /= norm;

  double d2 = -DOT(N2,V02);

  // Signed distances of T1's vertices to T2's plane
  double d01 = DOT(N2,V01) + d2;
  double d11 = DOT(N2,V11) + d2;
  double d21 = DOT(N2,V21) + d2;

  double d02 = DOT(N1,V02) + d1;
  double d12 = DOT(N1,V12) + d1;
  double d22 = DOT(N1,V22) + d1;

  // Round values near 0 to 0
  d01 = (fabs(d01) < 1e-10) ? 0 : d01;
  d11 = (fabs(d11) < 1e-10) ? 0 : d11;
  d21 = (fabs(d21) < 1e-10) ? 0 : d21;

  d02 = (fabs(d02) < 1e-10) ? 0 : d02;
  d12 = (fabs(d12) < 1e-10) ? 0 : d12;
  d22 = (fabs(d22) < 1e-10) ? 0 : d22;

  if (fabs(d01) + fabs(d11) + fabs(d21) < 3*tol ||
      fabs(d02) + fabs(d12) + fabs(d22) < 3*tol)
  {
    // Approximately coplanar; check if one triangle is inside the other /

    // Check if a point in T1 is inside T2
    bool inside = true;
    inside = inside && DOTCROSS4(N2, V12,V02, V01,V02) > 0;
    inside = inside && DOTCROSS4(N2, V02,V22, V01,V22) > 0;
    inside = inside && DOTCROSS4(N2, V22,V12, V01,V12) > 0;

    if (inside) return 0.;

    // Check if a point in T2 is inside T1
    inside = true;
    inside = inside && DOTCROSS4(N1, V11,V01, V02,V01) > 0;
    inside = inside && DOTCROSS4(N1, V01,V21, V02,V21) > 0;
    inside = inside && DOTCROSS4(N1, V21,V11, V02,V11) > 0;

    if (inside) return 0.;
  }

  bool noTouch = false;

  // Check for intersection with plane - one point should have opposite sign
  if (sgn(d01) == sgn(d11) && sgn(d01) == sgn(d21)) // && fabs(d01) > tol)
  {
    noTouch = true;

    // No intersection; check if projection of points provides closer distance
    if (fabs(d01) < dist)
    {
      double P01[3];
      for (int d = 0; d < 3; d++)
        P01[d] = V01[d] - N2[d]*d01;
      bool inside = true;
      inside = inside && DOTCROSS4(N2, V12,V02, P01,V02) > 0;
      inside = inside && DOTCROSS4(N2, V02,V22, P01,V22) > 0;
      inside = inside && DOTCROSS4(N2, V22,V12, P01,V12) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N2[i]*d01;
        dist = fabs(d01);
      }
    }

    if (fabs(d11) < dist)
    {
      double P11[3];
      for (int d = 0; d < 3; d++)
        P11[d] = V11[d] - N2[d]*d11;
      bool inside = true;
      inside = inside && DOTCROSS4(N2, V12,V02, P11,V02) > 0;
      inside = inside && DOTCROSS4(N2, V02,V22, P11,V22) > 0;
      inside = inside && DOTCROSS4(N2, V22,V12, P11,V12) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N2[i]*d11;
        dist = fabs(d11);
      }
    }

    if (fabs(d21) < dist)
    {
      double P21[3];
      for (int d = 0; d < 3; d++)
        P21[d] = V21[d] - N2[d]*d21;
      bool inside = true;
      inside = inside && DOTCROSS4(N2, V12,V02, P21,V02) > 0;
      inside = inside && DOTCROSS4(N2, V02,V22, P21,V22) > 0;
      inside = inside && DOTCROSS4(N2, V22,V12, P21,V12) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N2[i]*d21;
        dist = fabs(d21);
      }
    }
  }

  // Check for intersection with plane - one point should have opposite sign
  if (sgn(d02) == sgn(d12) && sgn(d02) == sgn(d22)) // && fabs(d02) > tol)
  {
    noTouch = true;

    // No intersection; check if projection of points provides closer distance
    if (fabs(d02) < dist)
    {
      double P02[3];
      for (int d = 0; d < 3; d++)
        P02[d] = V02[d] - N1[d]*d02;
      bool inside = true;
      inside = inside && DOTCROSS4(N1, V11,V01, P02,V01) > 0;
      inside = inside && DOTCROSS4(N1, V01,V21, P02,V21) > 0;
      inside = inside && DOTCROSS4(N1, V21,V11, P02,V11) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N1[i]*d02;
        dist = fabs(d02);
      }
    }

    if (fabs(d12) < dist)
    {
      double P12[3];
      for (int d = 0; d < 3; d++)
        P12[d] = V12[d] - N1[d]*d12;
      bool inside = true;
      inside = inside && DOTCROSS4(N1, V11,V01, P12,V01) > 0;
      inside = inside && DOTCROSS4(N1, V01,V21, P12,V21) > 0;
      inside = inside && DOTCROSS4(N1, V21,V11, P12,V11) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N1[i]*d12;
        dist = fabs(d12);
      }
    }

    if (fabs(d22) < dist)
    {
      double P22[3];
      for (int d = 0; d < 3; d++)
        P22[d] = V22[d] - N1[d]*d22;
      bool inside = true;
      inside = inside && DOTCROSS4(N1, V11,V01, P22,V01) > 0;
      inside = inside && DOTCROSS4(N1, V01,V21, P22,V21) > 0;
      inside = inside && DOTCROSS4(N1, V21,V11, P22,V11) > 0;

      if (inside)
      {
        for (int i = 0; i < 3; i++)
          minVec[i] = N1[i]*d22;
        dist = fabs(d22);
      }
    }
  }

  // No intersection; return result from edge intersections & plane projections
  if (noTouch)
    return dist;

  // Compute intersection line
  double L[3];
  CROSS(N1, N2, L);
  norm = NORM(L);
  for (int d = 0; d < 3; d++)
    L[d] /= norm;

  double p0 = DOT(L,V01);
  double p1 = DOT(L,V11);
  double p2 = DOT(L,V21);

  double q0 = DOT(L,V02);
  double q1 = DOT(L,V12);
  double q2 = DOT(L,V22);

  // Figure out which point of each triangle is opposite the other two
  int npt1 = (sgn(d01) != sgn(d11)) ? ( (sgn(d11) == sgn(d21)) ? 0 : 1 ) : 2;
  int npt2 = (sgn(d02) != sgn(d12)) ? ( (sgn(d12) == sgn(d22)) ? 0 : 1 ) : 2;

  double s1, s2;
  switch (npt1)
  {
    case 0:
      s1 = p1 + (p0-p1) * (d11 / (d11-d01));
      s2 = p2 + (p0-p2) * (d21 / (d21-d01));
      break;
    case 1:
      s1 = p0 + (p1-p0) * (d01 / (d01-d11));
      s2 = p2 + (p1-p2) * (d21 / (d21-d11));
      break;
    case 2:
      s1 = p0 + (p2-p0) * (d01 / (d01-d21));
      s2 = p1 + (p2-p1) * (d11 / (d11-d21));
      break;
  }

  double t1, t2;
  switch (npt2)
  {
    case 0:
      t1 = q1 + (q0-q1) * (d12 / (d12-d02));
      t2 = q2 + (q0-q2) * (d22 / (d22-d02));
      break;
    case 1:
      t1 = q0 + (q1-q0) * (d02 / (d02-d12));
      t2 = q2 + (q1-q2) * (d22 / (d22-d12));
      break;
    case 2:
      t1 = q0 + (q2-q0) * (d02 / (d02-d22));
      t2 = q1 + (q2-q1) * (d12 / (d12-d22));
      break;
  }

  s1 = (fabs(s1) < 1e-10) ? 0 : s1;
  s2 = (fabs(s2) < 1e-10) ? 0 : s2;
  t1 = (fabs(t1) < 1e-10) ? 0 : t1;
  t2 = (fabs(t2) < 1e-10) ? 0 : t2;

  if (s1 > s2)
    swap(s1,s2);

  if (t1 > t2)
    swap(t1,t2);

  if (s2 < t1 || t2 < s1)
  {
    // No overlap; return min of dt*L and minDist
    double dt = fmin(fabs(t1-s2), fabs(s1-t2));
    double dl = 0;
    for (int d = 0; d < 3; d++)
      dl += (dt*L[d])*(dt*L[d]);
    dl = sqrt(dl);

    if (dl < dist)
    {
      dist = dl;
      for (int i = 0; i < 3; i++)
        minVec[i] = sgn(t1-s2)*dt*L[i]; // Ensure vec is T1 -> T2
    }

    return dist;
  }

  return 0.;
}

static
__device__ __forceinline__
dPoint faceNormal(const double* xv)
{
  /* Assuming nodes of face ordered CCW such that right-hand rule gives
     * outward normal */

  // Triangle #1
  dPoint pt0 = dPoint(&xv[0]);
  dPoint pt1 = dPoint(&xv[3]);
  dPoint pt2 = dPoint(&xv[6]);
  dPoint norm1 = (pt1-pt0).cross(pt2-pt0);           // Face normal vector

  // Triangle #2
  pt1 = dPoint(&xv[9]);
  dPoint norm2 = (pt2-pt0).cross(pt1-pt0);

  // Average the two triangle's normals
  dPoint norm = 0.5*(norm1+norm2);

  return (norm / norm.norm());
}

template<int nSideC, int nSideF>
__device__
double intersectionCheck(dMeshBlock &mb, const double* __restrict__ fxv,
    const double* __restrict__ exv, double* __restrict__ minVec)
{
  /* --- Prerequisites --- */

  const int nvert = nSideC*nSideC*nSideC;
  const int nvertf = nSideF*nSideF;

  const int sorderC = nSideC-1;
  const int sorderF = nSideF-1;

  // NOTE: Structured ordering  |  btm,top,left,right,front,back
  short TriPts[12][3] = {{0,1,3},{0,3,2},{4,7,5},{4,6,7},{0,2,6},{0,6,4},
                       {1,3,7},{1,7,5},{0,4,5},{0,5,1},{2,3,7},{2,6,7}};

  double tol = 1e-9;
  double TC[9], TF[9];
  double minDist = BIG_DOUBLE;
  minVec[0] = minDist;
  minVec[1] = minDist;
  minVec[2] = minDist;

  double bboxC[6], bboxF[6];
  cuda_funcs::getBoundingBox<3,nvertf>(fxv, bboxF);
  cuda_funcs::getBoundingBox<3,nvert>(exv, bboxC);

  /* Only 3 cases possible:
   * 1) Face entirely contained within element
   * 2) Face intersects with element's boundary
   * 3) Face and element do not intersect
   */

  // 1) In case of face entirely inside element, check if a pt is inside ele
  if (cuda_funcs::boundingBoxCheck<3>(bboxC, bboxF, 0))
  {
    double rst[3];
    if (mb.getRefLoc<nSideC>(exv, bboxC, fxv, rst))
      return 0.;
  }

  // 2) Check outer faces of element for intersection with face
#pragma unroll
  for (int f = 0; f < 6; f++)
  {
#pragma unroll
    for (int g = 0; g < sorderC*sorderC; g++)
    {
      int I, J, K;
      switch (f)
      {
        case 0: // Bottom
          I = g / sorderC;
          J = g % sorderC;
          K = 0;
          break;
        case 1: // Top
          I = g / sorderC;
          J = g % sorderC;
          K = sorderC - 1;
          break;
        case 2: // Left
          I = 0;
          J = g / sorderC;
          K = g % sorderC;
          break;
        case 3: // Right
          I = sorderC - 1;
          J = g / sorderC;
          K = g % sorderC;
          break;
        case 4: // Front
          I = g / sorderC;
          J = 0;
          K = g % sorderC;
          break;
        case 5: // Back
          I = g / sorderC;
          J = sorderC - 1;
          K = g % sorderC;
          break;
      }

      int i0 = I+nSideC*(J+nSideC*K);
      int j0 = i0 + nSideC*nSideC;
      int lin2curv[8] = {i0, i0+1, i0+nSideC, i0+nSideC+1, j0, j0+1, j0+nSideC, j0+nSideC+1};
      for (int i = 0; i < 8; i++)
        lin2curv[i] = mb.ijk2gmsh[lin2curv[i]];

      // Get triangles for the sub-hex of the larger curved hex
      for (int i = f; i < f+2; i++)
      {
        for (int p = 0; p < 3; p++)
        {
          int ipt = lin2curv[TriPts[i][p]];
          for (int d = 0; d < 3; d++)
            TC[3*p+d] = exv[3*ipt+d];
        }

        cuda_funcs::getBoundingBox<3,3>(TC, bboxC);
        double btol = .05*(bboxC[3]-bboxC[0]+bboxC[4]-bboxC[1]+bboxC[5]-bboxC[2]);
        btol = fmin(btol, minDist);
        if (!cuda_funcs::boundingBoxCheck<3>(bboxC,bboxF,btol)) continue;

        for (int M = 0; M < sorderF; M++)
        {
          for (int N = 0; N < sorderF; N++)
          {
            int m0 = M + nSideF*N;
            int TriPtsF[2][3] = {{m0, m0+1, m0+nSideF+1}, {m0, m0+nSideF+1, m0+nSideF}};
            for (int m = 0; m < 2; m++)
              for (int n = 0; n < 3; n++)
                TriPtsF[m][n] = mb.ijk2gmsh_quad[TriPtsF[m][n]];

            // Intersection check between element face tris & cutting-face tris
            for (int j = 0; j < 2; j++)
            {
              for (int p = 0; p < 3; p++)
              {
                int ipt = TriPtsF[j][p];
                for (int d = 0; d < 3; d++)
                  TF[3*p+d] = fxv[3*ipt+d];
              }

              double vec[3];
              double dist = triTriDistance2(TF, TC, vec, tol);

              if (dist < tol)
                return 0.;

              if (dist < minDist)
              {
                for (int d = 0; d < 3; d++)
                  minVec[d] = vec[d];
                minDist = dist;
              }
            }
          }
        }
      }
    }
  }

  if (minDist == BIG_DOUBLE) // Definitely no intersection; use centroids to get vector
  {
    double tmp[3];
    cuda_funcs::getCentroid<3,nvert>(exv,minVec);
    cuda_funcs::getCentroid<3,nvertf>(fxv,tmp);

    minDist = 0;
    for (int d = 0; d < 3; d++)
    {
      minVec[d] -= tmp[d];
      minDist += minVec[d]*minVec[d];
    }

    return sqrt(minDist);
  }

  return minDist;
}

template<int nDims, int nSideC, int nSideF>
__global__
void fillCutMap(dMeshBlock mb, dvec<double> cutFaces, int nCut,
                int* __restrict__ cutFlag, int cutType)
{
  int ic = blockIdx.x * blockDim.x + threadIdx.x;

  if (ic >= mb.ncells) return;
//  if (ic >= 1) return;

  // Figure out how many threads are left in this block after ic>ncells returns
  int blockSize = min(blockDim.x, mb.ncells - blockIdx.x * blockDim.x);

  const int nvert = nSideC*nSideC*nSideC;
  const int nvertf = nSideF*nSideF;

  int myFlag = DC_UNASSIGNED;
  double myDist = BIG_DOUBLE;
  double myNorm[3] = {0., 0., 0.};
  double myDot;
  double nMin = 0;

  double xv[nDims*nvert];
  __shared__ double fxv[nDims*nvertf];

  // Load up the cell nodes into an array
  for (int i = 0; i < nvert; i++)
  {
    for (int d = 0; d < nDims; d++)
      xv[nDims*i+d] = mb.coord[ic+mb.ncells*(d+nDims*i)]; /// NOTE: 'row-major' ZEFR layout
  }

  double bboxC[2*nDims], bboxF[2*nDims];

  cuda_funcs::getBoundingBox<nDims,nvert>(xv, bboxC);

  // btol == 10 times the average side length of the cell's bounding box
  const double btol = (bboxC[3]-bboxC[0]+bboxC[4]-bboxC[1]+bboxC[5]-bboxC[2]); // / nDims * 10.
  const double dtol = 1e-3*btol;

  int stride = nDims*nvertf;

  for (int ff = 0; ff < nCut; ff++)
  {
    __syncthreads();

    for (int i = threadIdx.x; i < stride; i += blockSize)
    {
      fxv[i] = cutFaces[ff*stride+i];
    }

    __syncthreads();

    if (myFlag == DC_CUT) continue;

    /*if (mb.rrot)
      getBoundingBox(&cutFaces[ff*stride], nvertf, nDims, bbox, Rmat.data());
    else*/
    cuda_funcs::getBoundingBox<nDims,nvertf>(fxv, bboxF);

    if (myFlag != DC_CUT && cuda_funcs::boundingBoxCheck<nDims>(bboxC, bboxF, btol))
    {
      // Find distance from face to cell
      dPoint vec;
      double dist = intersectionCheck<nSideC,nSideF>(mb, fxv, xv, &vec[0]);
      vec /= dist;

      dPoint norm = faceNormal(fxv);

      if (dist < 1e-8*btol) // They intersect
      {
        myFlag = DC_CUT;
        myDist = 0.;
      }
      else if (myFlag == DC_UNASSIGNED || dist < (myDist - dtol))
      {
        // Unflagged cell, or have a closer face to use
        if (cutType == 0) norm *= -1;

        double dot = norm*vec;

        myDist = dist;
        for (int d = 0; d < 3; d++)
          myNorm[d] = norm[d];
        myDot = dot;
        nMin = 1;

        if (dot < 0) /// TODO: decide on standard orientation
          myFlag = DC_HOLE; // outwards normal = inside cutting surface
        else
          myFlag = DC_NORMAL;
      }
      else if (fabs(dist - myDist) <= dtol)
      {
        // Approx. same dist. to two faces; avg. their normals to decide
        if (cutType == 0) norm *= -1;

          myDist = dist;
          for (int d = 0; d < 3; d++)
            myNorm[d] = (nMin*myNorm[d] + norm[d]) / (nMin + 1.);
          nMin++;

          myDot = norm*vec;

          if (myDot < 0)
            myFlag = DC_HOLE; // outwards normal = inside cutting surface
          else
            myFlag = DC_NORMAL;
      }
      // else dist > myDist, ignore
    }
  }

//  if (myFlag == DC_CUT)
//    myFlag = (cutType == 1) ? DC_HOLE : DC_NORMAL;

  cutFlag[ic] = myFlag;
}

//void dMeshBlock::directCut(dvec<double> &cutFaces, int nCut, int nvertf, dCutMap &cutMap, int cutType)
void dMeshBlock::directCut(double* cutFaces_h, int nCut, int nvertf, int* cutFlag, int cutType)
{
  // Setup cutMap TODO: create initialization elsewhere?
  dvec<int> cutFlag_d;
  cutFlag_d.resize(ncells);

  dvec<double> cutFaces;
  cutFaces.assign(cutFaces_h, nCut*nvertf*nDims);

  int threads = 32;
  int blocks = (ncells + threads - 1) / threads;

  int nbShare = sizeof(double)*nvertf*nDims;

  if (ijk2gmsh_quad.size() != nvertf)
  {
    auto ijk2gmsh_quad_h = tg_funcs::structured_to_gmsh_quad(nvertf);
    ijk2gmsh_quad.assign(ijk2gmsh_quad_h.data(), nvertf);
  }

  switch(nvertf)
  {
    case 4:
      switch(nvert)
      {
        case 8:
          fillCutMap<3,2,2><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
          break;
//        case 27:
//          fillCutMap<3,3,2><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
        case 64:
          fillCutMap<3,4,2><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
          break;
//        case 125:
//          fillCutMap<3,5,4><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
        default:
          printf("nvert = %d\n",nvert);
          ThrowException("nvert case not implemented for directCut on device");
      }
      break;
//    case 9:
//      switch(nvert)
//      {
//        case 8:
//          fillCutMap<3,2,3><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
//        case 27:
//          fillCutMap<3,3,3><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
//        case 64:
//          fillCutMap<3,4,3><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
//        case 125:
//          fillCutMap<3,5,3><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
//        default:
//          printf("nvert = %d\n",nvert);
//          ThrowException("nvert case not implemented for directCut on device");
//      }
//      break;
    case 16:
      switch(nvert)
      {
        case 8:
          fillCutMap<3,2,4><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
          break;
//        case 27:
//          fillCutMap<3,3,4><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
        case 64:
          fillCutMap<3,4,4><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
          break;
//        case 125:
//          fillCutMap<3,5,4><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
        default:
          printf("nvert = %d\n",nvert);
          ThrowException("nvert case not implemented for directCut on device");
      }
      break;
//    case 25:
//      switch(nvert)
//      {
//        case 8:
//          fillCutMap<3,2,5><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
//        case 27:
//          fillCutMap<3,3,5><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
//        case 64:
//          fillCutMap<3,4,5><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
//        case 125:
//          fillCutMap<3,5,5><<<blocks, threads, nbShare>>>(*this, cutFaces, nCut, cutFlag_d.data(), cutType);
//          break;
//        default:
//          printf("nvert = %d\n",nvert);
//          ThrowException("nvert case not implemented for directCut on device");
//      }
//      break;
  }

  check_error();

  cuda_copy_d2h(cutFlag_d.data(), cutFlag, ncells);

  cutFaces.free_data();
  cutFlag_d.free_data();
}
