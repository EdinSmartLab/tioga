/*!
 * \file funcs.cpp
 * \brief Miscellaneous helper functions
 *
 * \author - Jacob Crabill
 *           Aerospace Computing Laboratory (ACL)
 *           Aero/Astro Department. Stanford University
 *
 * This file is part of the Tioga software library
 *
 * Tioga  is a tool for overset grid assembly on parallel distributed systems
 * Copyright (C) 2015-2016 Jay Sitaraman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits.h>
#include <map>
#include <omp.h>

#include "funcs.hpp"

static std::map<int, std::vector<int>> gmsh_maps_hex;
static std::map<int, std::vector<int>> gmsh_maps_quad;

std::vector<double> xlist;
std::vector<double> lag_i, lag_j, lag_k;
std::vector<double> dlag_i, dlag_j, dlag_k;

std::vector<int> ijk2gmsh;

std::vector<double> tmp_shape, tmp_dshape, tmp_weights;
std::vector<point> tmp_loc;

int shape_order = 0;

#define TOL 1e-10

namespace tg_funcs
{

point operator/(point a, double b) { return a/=b; }
point operator*(point a, double b) { return a*=b; }

bool operator<(const point &a, const point &b) { return a.x < b.x; }

std::ostream& operator<<(std::ostream &os, const point &pt)
{
  os << "(x,y,z) = " << pt.x << ", " << pt.y << ", " << pt.z;
  return os;
}

std::ostream& operator<<(std::ostream &os, const std::vector<int> &vec)
{
  for (auto &val:vec) std::cout << val << ", ";
  return os;
}

std::ostream& operator<<(std::ostream &os, const std::vector<double> &vec)
{
  for (auto &val:vec) std::cout << val << ", ";
  return os;
}

double Lagrange(std::vector<double> &x_lag, double y, uint mode)
{
  double lag = 1.0;

  for (uint i = 0; i < x_lag.size(); i++) {
    if (i != mode) {
      lag = lag*((y-x_lag[i])/(x_lag[mode]-x_lag[i]));
    }
  }

  return lag;
}

double dLagrange(std::vector<double> &x_lag, double y, uint mode)
{
  uint i, j;
  double dLag, dLag_num, dLag_den;

  dLag = 0.0;

  for (i=0; i<x_lag.size(); i++) {
    if (i!=mode) {
      dLag_num = 1.0;
      dLag_den = 1.0;

      for (j=0; j<x_lag.size(); j++) {
        if (j!=mode && j!=i) {
          dLag_num = dLag_num*(y-x_lag[j]);
        }

        if (j!=mode) {
          dLag_den = dLag_den*(x_lag[mode]-x_lag[j]);
        }
      }

      dLag = dLag+(dLag_num/dLag_den);
    }
  }

  return dLag;
}

// See Eigen's 'determinant.h' from 2014-9-18,
// https://bitbucket.org/eigen/eigen file Eigen/src/LU/determinant.h,
double det_3x3_part(const double* mat, int a, int b, int c)
{
  return mat[a] * (mat[3+b] * mat[6+c] - mat[3+c] * mat[6+b]);
}

double det_4x4_part(const double* mat, int j, int k, int m, int n)
{
  return (mat[j*4] * mat[k*4+1] - mat[k*4] * mat[j*4+1])
      * (mat[m*4+2] * mat[n*4+3] - mat[n*4+2] * mat[m*4+3]);
}

double det_2x2(const double* mat)
{
  return mat[0]*mat[3] - mat[1]*mat[2];
}

double det_3x3(const double* mat)
{
  return det_3x3_part(mat,0,1,2) - det_3x3_part(mat,1,0,2)
      + det_3x3_part(mat,2,0,1);
}

double det_4x4(const double* mat)
{
  return det_4x4_part(mat,0,1,2,3) - det_4x4_part(mat,0,2,1,3)
      + det_4x4_part(mat,0,3,1,2) + det_4x4_part(mat,1,2,0,3)
      - det_4x4_part(mat,1,3,0,2) + det_4x4_part(mat,2,3,0,1);
}

void adjoint_3x3(double *mat, double *adj)
{
  double a11 = mat[0], a12 = mat[1], a13 = mat[2];
  double a21 = mat[3], a22 = mat[4], a23 = mat[5];
  double a31 = mat[6], a32 = mat[7], a33 = mat[8];

  adj[0] = a22*a33 - a23*a32;
  adj[1] = a13*a32 - a12*a33;
  adj[2] = a12*a23 - a13*a22;

  adj[3] = a23*a31 - a21*a33;
  adj[4] = a11*a33 - a13*a31;
  adj[5] = a13*a21 - a11*a23;

  adj[6] = a21*a32 - a22*a31;
  adj[7] = a12*a31 - a11*a32;
  adj[8] = a11*a22 - a12*a21;
}

void adjoint_4x4(double *mat, double *adj)
{
  double a11 = mat[0],  a12 = mat[1],  a13 = mat[2],  a14 = mat[3];
  double a21 = mat[4],  a22 = mat[5],  a23 = mat[6],  a24 = mat[7];
  double a31 = mat[8],  a32 = mat[9],  a33 = mat[10], a34 = mat[11];
  double a41 = mat[12], a42 = mat[13], a43 = mat[14], a44 = mat[15];

  adj[0] = -a24*a33*a42 + a23*a34*a42 + a24*a32*a43 - a22*a34*a43 - a23*a32*a44 + a22*a33*a44;
  adj[1] =  a14*a33*a42 - a13*a34*a42 - a14*a32*a43 + a12*a34*a43 + a13*a32*a44 - a12*a33*a44;
  adj[2] = -a14*a23*a42 + a13*a24*a42 + a14*a22*a43 - a12*a24*a43 - a13*a22*a44 + a12*a23*a44;
  adj[3] =  a14*a23*a32 - a13*a24*a32 - a14*a22*a33 + a12*a24*a33 + a13*a22*a34 - a12*a23*a34;

  adj[4] =  a24*a33*a41 - a23*a34*a41 - a24*a31*a43 + a21*a34*a43 + a23*a31*a44 - a21*a33*a44;
  adj[5] = -a14*a33*a41 + a13*a34*a41 + a14*a31*a43 - a11*a34*a43 - a13*a31*a44 + a11*a33*a44;
  adj[6] =  a14*a23*a41 - a13*a24*a41 - a14*a21*a43 + a11*a24*a43 + a13*a21*a44 - a11*a23*a44;
  adj[7] = -a14*a23*a31 + a13*a24*a31 + a14*a21*a33 - a11*a24*a33 - a13*a21*a34 + a11*a23*a34;

  adj[8] = -a24*a32*a41 + a22*a34*a41 + a24*a31*a42 - a21*a34*a42 - a22*a31*a44 + a21*a32*a44;
  adj[9] =  a14*a32*a41 - a12*a34*a41 - a14*a31*a42 + a11*a34*a42 + a12*a31*a44 - a11*a32*a44;
  adj[10]= -a14*a22*a41 + a12*a24*a41 + a14*a21*a42 - a11*a24*a42 - a12*a21*a44 + a11*a22*a44;
  adj[11]=  a14*a22*a31 - a12*a24*a31 - a14*a21*a32 + a11*a24*a32 + a12*a21*a34 - a11*a22*a34;

  adj[12]=  a23*a32*a41 - a22*a33*a41 - a23*a31*a42 + a21*a33*a42 + a22*a31*a43 - a21*a32*a43;
  adj[13]= -a13*a32*a41 + a12*a33*a41 + a13*a31*a42 - a11*a33*a42 - a12*a31*a43 + a11*a32*a43;
  adj[14]=  a13*a22*a41 - a12*a23*a41 - a13*a21*a42 + a11*a23*a42 + a12*a21*a43 - a11*a22*a43;
  adj[15]= -a13*a22*a31 + a12*a23*a31 + a13*a21*a32 - a11*a23*a32 - a12*a21*a33 + a11*a22*a33;
}

std::vector<double> adjoint(const std::vector<double> &mat, unsigned int size)
{
  std::vector<double> adj(size*size);

  int signRow = -1;
  std::vector<double> Minor((size-1)*(size-1));
  for (int row=0; row<size; row++) {
    signRow *= -1;
    int sign = -1*signRow;
    for (int col=0; col<size; col++) {
      sign *= -1;
      // Setup the minor matrix (expanding along row, col)
      int i0 = 0;
      for (int i=0; i<size; i++) {
        if (i==row) continue;
        int j0 = 0;
        for (int j=0; j<size; j++) {
          if (j==col) continue;
          Minor[i0*(size-1)+j0] = mat[i*size+j];
          j0++;
        }
        i0++;
      }
      // Recall: adjoint is TRANSPOSE of cofactor matrix
      adj[col*size+row] = sign*determinant(Minor,size-1);
    }
  }

  return adj;
}

//! In-place adjoint function
void adjoint(const std::vector<double> &mat, std::vector<double> &adj, unsigned int size)
{
  adj.resize(size*size);

  int signRow = -1;
  std::vector<double> Minor((size-1)*(size-1));
  for (int row=0; row<size; row++) {
    signRow *= -1;
    int sign = -1*signRow;
    for (int col=0; col<size; col++) {
      sign *= -1;
      // Setup the minor matrix (expanding along row, col)
      int i0 = 0;
      for (int i=0; i<size; i++) {
        if (i==row) continue;
        int j0 = 0;
        for (int j=0; j<size; j++) {
          if (j==col) continue;
          Minor[i0*(size-1)+j0] = mat[i*size+j];
          j0++;
        }
        i0++;
      }
      // Recall: adjoint is TRANSPOSE of cofactor matrix
      adj[col*size+row] = sign*determinant(Minor,size-1);
    }
  }
}

double determinant(const std::vector<double> &data, unsigned int size)
{
  if (size == 1) {
    return data[0];
  }
  else if (size == 2) {
    // Base case
    return data[0]*data[3] - data[1]*data[2];
  }
  else if (size == 3) {
    return det_3x3(data.data());
  }
  else if (size == 4) {
    return det_4x4(data.data());
  }
  else {
    // Use minor-matrix recursion
    double Det = 0;
    int sign = -1;
    std::vector<double> Minor((size-1)*(size-1));
    for (int row=0; row<size; row++) {
      sign *= -1;
      // Setup the minor matrix (expanding along first column)
      int i0 = 0;
      for (int i=0; i<size; i++) {
        if (i==row) continue;
        for (int j=1; j<size; j++) {
          Minor[i0*(size-1)+j-1] = data[i*size+j];
        }
        i0++;
      }
      // Add in the minor's determinant
      Det += sign*determinant(Minor,size-1)*data[row*size+0];
    }

    return Det;
  }
}

void getBoundingBox(double *pts, int nPts, int nDims, double *bbox)
{
  for (int i=0; i<nDims; i++) {
    bbox[i]       =  INFINITY;
    bbox[nDims+i] = -INFINITY;
  }

  for (int i=0; i<nPts; i++) {
    for (int dim=0; dim<nDims; dim++) {
      bbox[dim]       = std::min(bbox[dim],      pts[i*nDims+dim]);
      bbox[nDims+dim] = std::max(bbox[nDims+dim],pts[i*nDims+dim]);
    }
  }
}

void getBoundingBox(double *pts, int nPts, int nDims, double *bbox, double *Smat)
{
  for (int i=0; i<nDims; i++) {
    bbox[i]       =  INFINITY;
    bbox[nDims+i] = -INFINITY;
  }

  std::vector<double> tmp_pt(nDims);
  for (int i = 0; i < nPts; i++) {
    // Apply transform to point
    for (int d1 = 0; d1 < nDims; d1++) {
      tmp_pt[d1] = 0;
      for (int d2 = 0; d2 < nDims; d2++)
        tmp_pt[d1] += Smat[nDims*d1+d2] * pts[i*nDims+d1];
    }

    for (int dim = 0; dim < nDims; dim++) {
      bbox[dim]       = std::min(bbox[dim],      tmp_pt[dim]);
      bbox[nDims+dim] = std::max(bbox[nDims+dim],tmp_pt[dim]);
    }
  }
}

std::vector<int> gmsh_to_structured_quad(unsigned int nNodes)
{
  std::vector<int> gmsh_to_ijk(nNodes,0);

  /* Lagrange Elements (or linear serendipity) */
  if (nNodes != 8)
  {
    int nNodes1D = sqrt(nNodes);

    if (nNodes1D * nNodes1D != nNodes)
      ThrowException("nNodes must be a square number.");

    int nLevels = nNodes1D / 2;

    /* Set shape values via recursive strategy (from Flurry) */
    int node = 0;
    for (int i = 0; i < nLevels; i++)
    {
      /* Corner Nodes */
      int i2 = (nNodes1D - 1) - i;
      gmsh_to_ijk[node]     = i  + nNodes1D * i;
      gmsh_to_ijk[node + 1] = i2 + nNodes1D * i;
      gmsh_to_ijk[node + 2] = i2 + nNodes1D * i2;
      gmsh_to_ijk[node + 3] = i  + nNodes1D * i2;

      node += 4;

      int nEdgeNodes = nNodes1D - 2 * (i + 1);
      for (int j = 0; j < nEdgeNodes; j++)
      {
        gmsh_to_ijk[node + j]                = i+1+j  + nNodes1D * i;
        gmsh_to_ijk[node + nEdgeNodes + j]   = i2     + nNodes1D * (i+1+j);
        gmsh_to_ijk[node + 2*nEdgeNodes + j] = i2-1-j + nNodes1D * i2;
        gmsh_to_ijk[node + 3*nEdgeNodes + j] = i      + nNodes1D * (i2-1-j);
      }

      node += 4 * nEdgeNodes;
    }

    /* Add center node in odd case */
    if (nNodes1D % 2 != 0)
    {
      gmsh_to_ijk[nNodes - 1] = nNodes1D/2 + nNodes1D * (nNodes1D/2);
    }
  }

  /* 8-node Serendipity Element */
  else
  {
    gmsh_to_ijk[0] = 0; gmsh_to_ijk[1] = 2;  gmsh_to_ijk[2] = 7;
    gmsh_to_ijk[3] = 5; gmsh_to_ijk[4] = 1;  gmsh_to_ijk[5] = 3;
    gmsh_to_ijk[6] = 4; gmsh_to_ijk[7] = 6;
  }

  return gmsh_to_ijk;
}

std::vector<int> structured_to_gmsh_quad(unsigned int nNodes)
{
  if (gmsh_maps_quad.count(nNodes))
  {
    return gmsh_maps_quad[nNodes];
  }
  else
  {
    auto gmsh2ijk = gmsh_to_structured_quad(nNodes);

    gmsh_maps_quad[nNodes] = reverse_map(gmsh2ijk);

    return gmsh_maps_quad[nNodes];
  }
}

std::vector<int> structured_to_gmsh_hex(unsigned int nNodes)
{
  if (gmsh_maps_hex.count(nNodes))
  {
    return gmsh_maps_hex[nNodes];
  }
  else
  {
    auto gmsh2ijk = gmsh_to_structured_hex(nNodes);

    gmsh_maps_hex[nNodes] = reverse_map(gmsh2ijk);

    return gmsh_maps_hex[nNodes];
  }
}

std::vector<int> gmsh_to_structured_hex(unsigned int nNodes)
{
  std::vector<int> gmsh_to_ijk(nNodes,0);

  int nSide = cbrt(nNodes);

  if (nSide*nSide*nSide != nNodes)
  {
    std::cout << "nNodes = " << nNodes << std::endl;
    ThrowException("For Lagrange hex of order N, must have (N+1)^3 shape points.");
  }

  std::vector<double> xlist(nSide);
  double dxi = 2./(nSide-1);

  for (int i=0; i<nSide; i++)
    xlist[i] = -1. + i*dxi;

  int nLevels = nSide / 2;
  int isOdd = nSide % 2;

  /* Recursion for all high-order Lagrange elements:
             * 8 corners, each edge's points, interior face points, volume points */
  int nPts = 0;
  for (int i = 0; i < nLevels; i++) {
    // Corners
    int i2 = (nSide-1) - i;
    gmsh_to_ijk[nPts+0] = i  + nSide * (i  + nSide * i);
    gmsh_to_ijk[nPts+1] = i2 + nSide * (i  + nSide * i);
    gmsh_to_ijk[nPts+2] = i2 + nSide * (i2 + nSide * i);
    gmsh_to_ijk[nPts+3] = i  + nSide * (i2 + nSide * i);
    gmsh_to_ijk[nPts+4] = i  + nSide * (i  + nSide * i2);
    gmsh_to_ijk[nPts+5] = i2 + nSide * (i  + nSide * i2);
    gmsh_to_ijk[nPts+6] = i2 + nSide * (i2 + nSide * i2);
    gmsh_to_ijk[nPts+7] = i  + nSide * (i2 + nSide * i2);
    nPts += 8;

    // Edges
    int nSide2 = nSide - 2 * (i+1);
    for (int j = 0; j < nSide2; j++) {
      // Edges around 'bottom'
      gmsh_to_ijk[nPts+0*nSide2+j] = i+1+j  + nSide * (i     + nSide * i);
      gmsh_to_ijk[nPts+3*nSide2+j] = i2     + nSide * (i+1+j + nSide * i);
      gmsh_to_ijk[nPts+5*nSide2+j] = i2-1-j + nSide * (i2    + nSide * i);
      gmsh_to_ijk[nPts+1*nSide2+j] = i      + nSide * (i+1+j + nSide * i);

      // 'Vertical' edges
      gmsh_to_ijk[nPts+2*nSide2+j] = i  + nSide * (i  + nSide * (i+1+j));
      gmsh_to_ijk[nPts+4*nSide2+j] = i2 + nSide * (i  + nSide * (i+1+j));
      gmsh_to_ijk[nPts+6*nSide2+j] = i2 + nSide * (i2 + nSide * (i+1+j));
      gmsh_to_ijk[nPts+7*nSide2+j] = i  + nSide * (i2 + nSide * (i+1+j));

      // Edges around 'top'
      gmsh_to_ijk[nPts+ 8*nSide2+j] = i+1+j  + nSide * (i     + nSide * i2);
      gmsh_to_ijk[nPts+10*nSide2+j] = i2     + nSide * (i+1+j + nSide * i2);
      gmsh_to_ijk[nPts+11*nSide2+j] = i2-1-j + nSide * (i2    + nSide * i2);
      gmsh_to_ijk[nPts+ 9*nSide2+j] = i      + nSide * (i+1+j + nSide * i2);
    }
    nPts += 12*nSide2;

    /* --- Faces [Use recursion from quadrilaterals] --- */

    int nLevels2 = nSide2 / 2;
    int isOdd2 = nSide2 % 2;

    // --- Bottom face ---
    for (int j0 = 0; j0 < nLevels2; j0++) {
      // Corners
      int j = j0 + i + 1;
      int j2 = i + 1 + (nSide2-1) - j0;
      gmsh_to_ijk[nPts+0] = j  + nSide * (j  + nSide * i);
      gmsh_to_ijk[nPts+1] = j  + nSide * (j2 + nSide * i);
      gmsh_to_ijk[nPts+2] = j2 + nSide * (j2 + nSide * i);
      gmsh_to_ijk[nPts+3] = j2 + nSide * (j  + nSide * i);
      nPts += 4;

      // Edges: Bottom, right, top, left
      int nSide3 = nSide2 - 2 * (j0+1);
      for (int k = 0; k < nSide3; k++) {
        gmsh_to_ijk[nPts+0*nSide3+k] = j      + nSide * (j+1+k  + nSide * i);
        gmsh_to_ijk[nPts+1*nSide3+k] = j+1+k  + nSide * (j2     + nSide * i);
        gmsh_to_ijk[nPts+2*nSide3+k] = j2     + nSide * (j2-1-k + nSide * i);
        gmsh_to_ijk[nPts+3*nSide3+k] = j2-1-k + nSide * (j      + nSide * i);
      }
      nPts += 4*nSide3;
    }

    // Center node for even-ordered Lagrange quads (odd value of nSide)
    if (isOdd2) {
      gmsh_to_ijk[nPts] = nSide/2 +  nSide*(nSide/2) +  nSide*nSide*i;
      nPts += 1;
    }

    // --- Front face ---
    for (int j0 = 0; j0 < nLevels2; j0++) {
      // Corners
      int j = j0 + i + 1;
      int j2 = i + 1 + (nSide2-1) - j0;
      gmsh_to_ijk[nPts+0] = j  + nSide * (i + nSide * j);
      gmsh_to_ijk[nPts+1] = j2 + nSide * (i + nSide * j);
      gmsh_to_ijk[nPts+2] = j2 + nSide * (i + nSide * j2);
      gmsh_to_ijk[nPts+3] = j  + nSide * (i + nSide * j2);
      nPts += 4;

      // Edges: Bottom, right, top, left
      int nSide3 = nSide2 - 2 * (j0+1);
      for (int k = 0; k < nSide3; k++) {
        gmsh_to_ijk[nPts+0*nSide3+k] = j+1+k  + nSide * (i + nSide * j);
        gmsh_to_ijk[nPts+1*nSide3+k] = j2     + nSide * (i + nSide * (j+1+k));
        gmsh_to_ijk[nPts+2*nSide3+k] = j2-1-k + nSide * (i + nSide * j2);
        gmsh_to_ijk[nPts+3*nSide3+k] = j      + nSide * (i + nSide * (j2-1-k));
      }
      nPts += 4*nSide3;
    }

    // Center node for even-ordered Lagrange quads (odd value of nSide)
    if (isOdd2) {
      gmsh_to_ijk[nPts] = nSide/2 + nSide*(i + nSide*(nSide/2));
      nPts += 1;
    }

    // --- Left face ---
    for (int j0 = 0; j0 < nLevels2; j0++) {
      // Corners
      int j = j0 + i + 1;
      int j2 = i + 1 + (nSide2-1) - j0;
      gmsh_to_ijk[nPts+0] = i + nSide * (j  + nSide * j);
      gmsh_to_ijk[nPts+1] = i + nSide * (j  + nSide * j2);
      gmsh_to_ijk[nPts+2] = i + nSide * (j2 + nSide * j2);
      gmsh_to_ijk[nPts+3] = i + nSide * (j2 + nSide * j);
      nPts += 4;

      // Edges: Bottom, right, top, left
      int nSide3 = nSide2 - 2 * (j0+1);
      for (int k = 0; k < nSide3; k++) {
        gmsh_to_ijk[nPts+0*nSide3+k] = i + nSide * (j      + nSide * (j+1+k));
        gmsh_to_ijk[nPts+1*nSide3+k] = i + nSide * (j+1+k  + nSide * j2);
        gmsh_to_ijk[nPts+2*nSide3+k] = i + nSide * (j2     + nSide * (j2-1-k));
        gmsh_to_ijk[nPts+3*nSide3+k] = i + nSide * (j2-1-k + nSide * j);
      }
      nPts += 4*nSide3;
    }

    // Center node for even-ordered Lagrange quads (odd value of nSide)
    if (isOdd2) {
      gmsh_to_ijk[nPts] = i + nSide * (nSide/2 + nSide * (nSide/2));
      nPts += 1;
    }

    // --- Right face ---
    for (int j0 = 0; j0 < nLevels2; j0++) {
      // Corners
      int j = j0 + i + 1;
      int j2 = i + 1 + (nSide2-1) - j0;
      gmsh_to_ijk[nPts+0] = i2 + nSide * (j  + nSide * j);
      gmsh_to_ijk[nPts+1] = i2 + nSide * (j2 + nSide * j);
      gmsh_to_ijk[nPts+2] = i2 + nSide * (j2 + nSide * j2);
      gmsh_to_ijk[nPts+3] = i2 + nSide * (j  + nSide * j2);
      nPts += 4;

      // Edges: Bottom, right, top, left
      int nSide3 = nSide2 - 2 * (j0+1);
      for (int k = 0; k < nSide3; k++) {
        gmsh_to_ijk[nPts+0*nSide3+k] = i2 + nSide * (j+1+k  + nSide * j);
        gmsh_to_ijk[nPts+1*nSide3+k] = i2 + nSide * (j2     + nSide * (j+1+k));
        gmsh_to_ijk[nPts+2*nSide3+k] = i2 + nSide * (j2-1-k + nSide * j2);
        gmsh_to_ijk[nPts+3*nSide3+k] = i2 + nSide * (j      + nSide * (j2-1-k));
      }
      nPts += 4*nSide3;
    }

    // Center node for even-ordered Lagrange quads (odd value of nSide)
    if (isOdd2) {
      gmsh_to_ijk[nPts] = i2 + nSide * (nSide/2 + nSide * (nSide/2));
      nPts += 1;
    }

    // --- Back face ---
    for (int j0 = 0; j0 < nLevels2; j0++) {
      // Corners
      int j = j0 + i + 1;
      int j2 = i + 1 + (nSide2-1) - j0;
      gmsh_to_ijk[nPts+0] = j2 + nSide * (i2 + nSide * j);
      gmsh_to_ijk[nPts+1] = j  + nSide * (i2 + nSide * j);
      gmsh_to_ijk[nPts+2] = j  + nSide * (i2 + nSide * j2);
      gmsh_to_ijk[nPts+3] = j2 + nSide * (i2 + nSide * j2);
      nPts += 4;

      // Edges: Bottom, right, top, left
      int nSide3 = nSide2 - 2 * (j0+1);
      for (int k = 0; k < nSide3; k++) {
        gmsh_to_ijk[nPts+0*nSide3+k] = j2-1-k + nSide * (i2 + nSide*j);
        gmsh_to_ijk[nPts+1*nSide3+k] = j      + nSide * (i2 + nSide*(j+1+k));
        gmsh_to_ijk[nPts+2*nSide3+k] = j+1+k  + nSide * (i2 + nSide*j2);
        gmsh_to_ijk[nPts+3*nSide3+k] = j2     + nSide * (i2 + nSide*(j2-1-k));
      }
      nPts += 4*nSide3;
    }

    // Center node for even-ordered Lagrange quads (odd value of nSide)
    if (isOdd2) {
      gmsh_to_ijk[nPts] = nSide/2 + nSide * (i2 + nSide * (nSide/2));
      nPts += 1;
    }

    // --- Top face ---
    for (int j0 = 0; j0 < nLevels2; j0++) {
      // Corners
      int j = j0 + i + 1;
      int j2 = i + 1 + (nSide2-1) - j0;
      gmsh_to_ijk[nPts+0] = j  + nSide * (j  + nSide * i2);
      gmsh_to_ijk[nPts+1] = j2 + nSide * (j  + nSide * i2);
      gmsh_to_ijk[nPts+2] = j2 + nSide * (j2 + nSide * i2);
      gmsh_to_ijk[nPts+3] = j  + nSide * (j2 + nSide * i2);
      nPts += 4;

      // Edges: Bottom, right, top, left
      int nSide3 = nSide2 - 2 * (j0+1);
      for (int k = 0; k < nSide3; k++) {
        gmsh_to_ijk[nPts+0*nSide3+k] = j+1+k  + nSide * (j      + nSide * i2);
        gmsh_to_ijk[nPts+1*nSide3+k] = j2     + nSide * (j+1+k  + nSide * i2);
        gmsh_to_ijk[nPts+2*nSide3+k] = j2-1-k + nSide * (j2     + nSide * i2);
        gmsh_to_ijk[nPts+3*nSide3+k] = j      + nSide * (j2-1-k + nSide * i2);
      }
      nPts += 4*nSide3;
    }

    // Center node for even-ordered Lagrange quads (odd value of nSide)
    if (isOdd2) {
      gmsh_to_ijk[nPts] = nSide/2 + nSide * (nSide/2 +  nSide * i2);
      nPts += 1;
    }
  }

  // Center node for even-ordered Lagrange quads (odd value of nSide)
  if (isOdd) {
    gmsh_to_ijk[nNodes-1] = nSide/2 + nSide * (nSide/2 + nSide * (nSide/2));
  }

  return gmsh_to_ijk;
}

std::vector<int> reverse_map(const std::vector<int> &map1)
{
  auto map2 = map1;
  for (int i = 0; i < map1.size(); i++)
    map2[i] = findFirst(map1, i);

  return map2;
}

std::vector<int> get_int_list(int N, int start)
{
  std::vector<int> list(N);
  for (int i = 0; i < N; i++)
    list[i] = start + i;

  return list;
}

std::vector<uint> get_int_list(uint N, uint start)
{
  std::vector<uint> list(N);
  for (uint i = 0; i < N; i++)
    list[i] = start + i;

  return list;
}

std::vector<double> shape;
std::vector<double> dshape;
std::vector<double> grad;
std::vector<double> ginv;

bool getRefLocNewton(double *xv, double *in_xyz, double *out_rst, int nNodes, int nDims)
{
  // First, do a quick check to see if the point is even close to being in the element
  double xmin, ymin, zmin;
  double xmax, ymax, zmax;
  xmin = ymin = zmin =  1e15;
  xmax = ymax = zmax = -1e15;
  double eps = 1e-10;

  double box[6];
  getBoundingBox(xv, nNodes, nDims, box);
  xmin = box[0];  ymin = box[1];  zmin = box[2];
  xmax = box[3];  ymax = box[4];  zmax = box[5];

  point pos = point(in_xyz);
  /* --- Want the closest reference location to the given point, so don't just give up --- */
//  if (pos.x < xmin-eps || pos.y < ymin-eps || pos.z < zmin-eps ||
//      pos.x > xmax+eps || pos.y > ymax+eps || pos.z > zmax+eps) {
//    // Point does not lie within cell - return an obviously bad ref position
//    for (int i = 0; i < nDims; i++) out_rst[i] = 99.;
//    return false;
//  }

  // Use a relative tolerance to handle extreme grids
  double h = std::min(xmax-xmin,ymax-ymin);
  if (nDims==3) h = std::min(h,zmax-zmin);

//  double tol = 1e-12*h;
  double tol = 1e-10*h;

  shape.resize(nNodes);
  dshape.resize(nNodes*nDims);
  grad.resize(nDims*nDims);
  ginv.resize(nDims*nDims);

  int iter = 0;
  int iterMax = 20;
  double norm = 1;
  double norm_prev = 2;

  // Starting location: {0,0,0}
  for (int i = 0; i < nDims; i++) out_rst[i] = 0;
  point loc = point(out_rst,nDims);

  while (norm > tol && iter<iterMax) {
    if (nDims == 2) {
      shape_quad(loc,shape.data(),nNodes);
      dshape_quad(loc,dshape.data(),nNodes);
    } else {
      shape_hex(loc,shape.data(),nNodes);
      dshape_hex(loc,dshape.data(),nNodes);
    }

    point dx = pos;
    grad.assign(nDims*nDims,0.);

    for (int n=0; n<nNodes; n++) {
      for (int i=0; i<nDims; i++) {
        for (int j=0; j<nDims; j++) {
          grad[i*nDims+j] += xv[n*nDims+i]*dshape[n*nDims+j];
        }
        dx[i] -= shape[n]*xv[n*nDims+i];
      }
    }

    double detJ = determinant(grad,nDims);

    adjoint_3x3(grad.data(),ginv.data());

    double delta[3] = {0,0,0};
    for (int i=0; i<nDims; i++)
      for (int j=0; j<nDims; j++)
        delta[i] += ginv[i*nDims+j]*dx[j]/detJ;

    norm = dx.norm();
    for (int i=0; i<nDims; i++)
      loc[i] = std::max(std::min(loc[i]+delta[i],1.01),-1.01);

    if (iter > 1 && norm > .99*norm_prev) // If it's clear we're not converging
      break;

    norm_prev = norm;

    iter++;
  }

  for (int i = 0; i < nDims; i++)
    out_rst[i] = loc[i];

  if (std::max( std::abs(loc[0]), std::max( std::abs(loc[1]), std::abs(loc[2]) ) ) <= 1.+eps)
    return true;
  else
    return false;
}


double computeVolume(double *xv, int nNodes, int nDims)
{
  if (nDims == 2)
  {
    int order = std::max((int)sqrt(nNodes)-1, 0);
    if (order != shape_order)
    {
      tmp_loc = getLocSpts(QUAD,order,std::string("Legendre"));
      shape_order = order;
    }
  }
  else
  {
    int order = std::max((int)cbrt(nNodes)-1, 0);
    if (order != shape_order)
    {
      tmp_loc = getLocSpts(HEX,order,std::string("Legendre"));
      shape_order = order;
    }
  }

  uint nSpts = tmp_loc.size();

  if (tmp_weights.size() != nSpts)
    tmp_weights = getQptWeights(shape_order, nDims);

  if (tmp_shape.size() != nSpts*nNodes || tmp_dshape.size() != nSpts*nNodes*nDims)
  {
    // Note: for a given element type and shape order, these don't change
    tmp_shape.resize(nSpts*nNodes);
    tmp_dshape.resize(nSpts*nNodes*nDims);

    if (nDims == 2)
    {
      for (uint spt = 0; spt < nSpts; spt++)
      {
        shape_quad(tmp_loc[spt], &tmp_shape[spt*nNodes], nNodes);
        dshape_quad(tmp_loc[spt], &tmp_dshape[spt*nNodes*nDims], nNodes);
      }
    }
    else
    {
      for (uint spt = 0; spt < nSpts; spt++)
      {
        shape_hex(tmp_loc[spt], &tmp_shape[spt*nNodes], nNodes);
        dshape_hex(tmp_loc[spt], &tmp_dshape[spt*nNodes*nDims], nNodes);
      }
    }
  }

  double vol = 0.;

  for (uint spt = 0; spt < nSpts; spt++)
  {
    double jaco[9] = {0.0};
    for (uint n = 0; n < nNodes; n++)
      for (uint d1 = 0; d1 < nDims; d1++)
        for (uint d2 = 0; d2 < nDims; d2++)
          jaco[d1*nDims+d2] += tmp_dshape[(spt*nNodes+n)*nDims+d2] * xv[n*nDims+d1];

    double detJac = 0;
    if (nDims == 2)
      detJac = det_2x2(jaco);
    else
      detJac = det_3x3(jaco);

    if (detJac<0) FatalError("TIOGA: computeVolume: Negative Jacobian at quadrature point.");

    vol += detJac * tmp_weights[spt];
  }

  return vol;
}

//! Assuming 4-point quad face or 2-point line, calculate unit 'outward' normal
Vec3 faceNormal(double* xv, int nDims)
{
  if (nDims == 3)
  {
    /* Assuming nodes of face ordered CCW such that right-hand rule gives
     * outward normal */

    // Triangle #1
    point pt0 = point(&xv[0]);
    point pt1 = point(&xv[3]);
    point pt2 = point(&xv[6]);
    Vec3 a = pt1 - pt0;
    Vec3 b = pt2 - pt0;
    Vec3 norm1 = a.cross(b);           // Face normal vector

    // Triangle #2
    pt1 = point(&xv[9]);
    a = pt1 - pt0;
    Vec3 norm2 = b.cross(a);

    // Average the two triangle's normals
    Vec3 norm = (norm1+norm2)/2.;
    norm /= norm.norm();

    return norm;
  }
  else
  {
    /* Assuming nodes of face taken from CCW ordering within cell
     * (i.e. cell center is to 'left' of vector from pt1 to pt2) */
    point pt1 = point(&xv[0],2);
    point pt2 = point(&xv[2],2);
    Vec3 dx = pt2 - pt1;
    Vec3 norm = Vec3({-dx.y,dx.x,0});
    norm /= norm.norm();

    return norm;
  }
}

void shape_line(double xi, std::vector<double> &out_shape, int nNodes)
{
  out_shape.resize(nNodes);
  shape_line(xi, out_shape.data(), nNodes);
}

void shape_line(double xi, double* out_shape, int nNodes)
{
  std::vector<double> xlist(nNodes);
  double dxi = 2./(nNodes-1);

  for (int i = 0; i < nNodes; i++)
    xlist[i] = -1. + i*dxi;

  for (int i = 0; i < nNodes; i++)
    out_shape[i] = Lagrange(xlist, xi, i);
}

void shape_quad(const point &in_rs, std::vector<double> &out_shape, int nNodes)
{
  out_shape.resize(nNodes);
  shape_quad(in_rs, out_shape.data(), nNodes);
}

void shape_quad(const point &in_rs, double* out_shape, int nNodes)
{
  double xi  = in_rs.x;
  double eta = in_rs.y;

  int nSide = sqrt(nNodes);

  if (nSide*nSide != nNodes)
    FatalError("For Lagrange quad of order N, must have (N+1)^2 shape points.");

  if (xlist.size() != nSide)
  {
    xlist.resize(nSide);
    double dxi = 2./(nSide-1);

    for (int i=0; i<nSide; i++)
      xlist[i] = -1. + i*dxi;
  }

  int nLevels = nSide / 2;
  int isOdd = nSide % 2;

  // Pre-compute Lagrange values to avoid redundant calculations
  lag_i.resize(nSide);
  lag_j.resize(nSide);
  for (int i = 0; i < nSide; i++)
  {
    lag_i[i] = Lagrange(xlist,  xi, i);
    lag_j[i] = Lagrange(xlist, eta, i);
  }

  /* Recursion for all high-order Lagrange elements:
   * 4 corners, each edge's points, interior points */
  int nPts = 0;
  for (int i = 0; i < nLevels; i++) {
    // Corners
    int i2 = (nSide-1) - i;
    out_shape[nPts+0] = lag_i[i]  * lag_j[i];
    out_shape[nPts+1] = lag_i[i2] * lag_j[i];
    out_shape[nPts+2] = lag_i[i2] * lag_j[i2];
    out_shape[nPts+3] = lag_i[i]  * lag_j[i2];
    nPts += 4;

    // Edges: Bottom, right, top, left
    int nSide2 = nSide - 2 * (i+1);
    for (int j = 0; j < nSide2; j++) {
      out_shape[nPts+0*nSide2+j] = lag_i[i+1+j]  * lag_j[i];
      out_shape[nPts+1*nSide2+j] = lag_i[i2]     * lag_j[i+1+j];
      out_shape[nPts+2*nSide2+j] = lag_i[i2-1-j] * lag_j[i2];
      out_shape[nPts+3*nSide2+j] = lag_i[i]      * lag_j[i2-1-j];
    }
    nPts += 4*nSide2;
  }

  // Center node for even-ordered Lagrange quads (odd value of nSide)
  if (isOdd) {
    out_shape[nNodes-1] = lag_i[nSide/2] * lag_j[nSide/2];
  }
}

void shape_hex(const point &in_rst, std::vector<double> &out_shape, int nNodes)
{
  out_shape.resize(nNodes);
  shape_hex(in_rst, out_shape.data(), nNodes);
}

void shape_hex(const point &in_rst, double* out_shape, int nNodes)
{
  double xi  = in_rst.x;
  double eta = in_rst.y;
  double mu = in_rst.z;

  if (nNodes == 20) {
    double XI[8]  = {-1,1,1,-1,-1,1,1,-1};
    double ETA[8] = {-1,-1,1,1,-1,-1,1,1};
    double MU[8]  = {-1,-1,-1,-1,1,1,1,1};
    // Corner nodes
    for (int i=0; i<8; i++) {
      out_shape[i] = .125*(1+xi*XI[i])*(1+eta*ETA[i])*(1+mu*MU[i])*(xi*XI[i]+eta*ETA[i]+mu*MU[i]-2);
    }
    // Edge nodes, xi = 0
    out_shape[8]  = .25*(1-xi*xi)*(1-eta)*(1-mu);
    out_shape[10] = .25*(1-xi*xi)*(1+eta)*(1-mu);
    out_shape[16] = .25*(1-xi*xi)*(1-eta)*(1+mu);
    out_shape[18] = .25*(1-xi*xi)*(1+eta)*(1+mu);
    // Edge nodes, eta = 0
    out_shape[9]  = .25*(1-eta*eta)*(1+xi)*(1-mu);
    out_shape[11] = .25*(1-eta*eta)*(1-xi)*(1-mu);
    out_shape[17] = .25*(1-eta*eta)*(1+xi)*(1+mu);
    out_shape[19] = .25*(1-eta*eta)*(1-xi)*(1+mu);
    // Edge Nodes, mu = 0
    out_shape[12] = .25*(1-mu*mu)*(1-xi)*(1-eta);
    out_shape[13] = .25*(1-mu*mu)*(1+xi)*(1-eta);
    out_shape[14] = .25*(1-mu*mu)*(1+xi)*(1+eta);
    out_shape[15] = .25*(1-mu*mu)*(1-xi)*(1+eta);
  }
  else {
    int nSide = cbrt(nNodes);

    if (nSide*nSide*nSide != nNodes)
    {
      std::cout << "nNodes = " << nNodes << std::endl;
      ThrowException("For Lagrange hex of order N, must have (N+1)^3 shape points.");
    }

    if (xlist.size() != nSide)
    {
      xlist.resize(nSide);
      double dxi = 2./(nSide-1);

      for (int i=0; i<nSide; i++)
        xlist[i] = -1. + i*dxi;
    }

    if (ijk2gmsh.size() != nNodes)
      ijk2gmsh = structured_to_gmsh_hex(nNodes);

    // Pre-compute Lagrange function values to avoid redundant calculations
    lag_i.resize(nSide);
    lag_j.resize(nSide);
    lag_k.resize(nSide);

#pragma omp parallel for
    for (int i = 0; i < nSide; i++)
    {
      lag_i[i] = Lagrange(xlist,  xi, i);
      lag_j[i] = Lagrange(xlist, eta, i);
      lag_k[i] = Lagrange(xlist,  mu, i);
    }

#pragma omp parallel for collapse(3)
    for (int k = 0; k < nSide; k++)
      for (int j = 0; j < nSide; j++)
        for (int i = 0; i < nSide; i++)
          out_shape[ijk2gmsh[i+nSide*(j+nSide*k)]] = lag_i[i] * lag_j[j] * lag_k[k];
  }
}

void dshape_quad(const std::vector<point> &loc_pts, double* out_dshape, int nNodes)
{
  for (int i = 0; i < loc_pts.size(); i++)
    dshape_quad(loc_pts[i], &out_dshape[i*nNodes*2], nNodes);
}

void dshape_quad(const point &in_rs, double* out_dshape, int nNodes)
{
  double xi  = in_rs.x;
  double eta = in_rs.y;

  int nSide = sqrt(nNodes);

  if (nSide*nSide != nNodes)
    FatalError("For Lagrange quad of order N, must have (N+1)^2 shape points.");

  if (xlist.size() != nSide)
  {
    xlist.resize(nSide);
    double dxi = 2./(nSide-1);

    for (int i=0; i<nSide; i++)
      xlist[i] = -1. + i*dxi;
  }

  int nLevels = nSide / 2;
  int isOdd = nSide % 2;

  if (lag_i.size() != nSide || dlag_i.size() != nSide)
  {
    lag_i.resize(nSide); dlag_i.resize(nSide);
    lag_j.resize(nSide); dlag_j.resize(nSide);
  }

  for (int i = 0; i < nSide; i++)
  {
    lag_i[i] = Lagrange(xlist, xi, i);
    lag_j[i] = Lagrange(xlist, eta, i);
    dlag_i[i] = dLagrange(xlist, xi, i);
    dlag_j[i] = dLagrange(xlist, eta, i);
  }

  /* Recursion for all high-order Lagrange elements:
     * 4 corners, each edge's points, interior points */
  int nPts = 0;
  for (int i = 0; i < nLevels; i++) {
    // Corners
    int i2 = (nSide-1) - i;
    out_dshape[2*(nPts+0)+0] = dlag_i[i]  * lag_j[i];
    out_dshape[2*(nPts+1)+0] = dlag_i[i2] * lag_j[i];
    out_dshape[2*(nPts+2)+0] = dlag_i[i2] * lag_j[i2];
    out_dshape[2*(nPts+3)+0] = dlag_i[i]  * lag_j[i2];

    out_dshape[2*(nPts+0)+1] = lag_i[i]  * dlag_j[i];
    out_dshape[2*(nPts+1)+1] = lag_i[i2] * dlag_j[i];
    out_dshape[2*(nPts+2)+1] = lag_i[i2] * dlag_j[i2];
    out_dshape[2*(nPts+3)+1] = lag_i[i]  * dlag_j[i2];
    nPts += 4;

    // Edges
    int nSide2 = nSide - 2 * (i+1);
    for (int j = 0; j < nSide2; j++) {
      out_dshape[2*(nPts+0*nSide2+j)] = dlag_i[i+1+j]  * lag_j[i];
      out_dshape[2*(nPts+1*nSide2+j)] = dlag_i[i2]     * lag_j[i+1+j];
      out_dshape[2*(nPts+2*nSide2+j)] = dlag_i[i2-1-j] * lag_j[i2];
      out_dshape[2*(nPts+3*nSide2+j)] = dlag_i[i]      * lag_j[i2-1-j];

      out_dshape[2*(nPts+0*nSide2+j)+1] = lag_i[i+1+j]  * dlag_j[i];
      out_dshape[2*(nPts+1*nSide2+j)+1] = lag_i[i2]     * dlag_j[i+1+j];
      out_dshape[2*(nPts+2*nSide2+j)+1] = lag_i[i2-1-j] * dlag_j[i2];
      out_dshape[2*(nPts+3*nSide2+j)+1] = lag_i[i]      * dlag_j[i2-1-j];
    }
    nPts += 4*nSide2;
  }

  // Center node for even-ordered Lagrange quads (odd value of nSide)
  if (isOdd) {
    out_dshape[2*(nNodes-1)+0] = dlag_i[nSide/2] * lag_j[nSide/2];
    out_dshape[2*(nNodes-1)+1] = lag_i[nSide/2] * dlag_j[nSide/2];
  }
}

void dshape_hex(const std::vector<point> &loc_pts, double* out_dshape, int nNodes)
{
  for (int i = 0; i < loc_pts.size(); i++) {
    point pt = loc_pts[i];
    dshape_hex(pt, &out_dshape[i*nNodes*3], nNodes);
  }
}

void dshape_hex(const point &in_rst, double* out_dshape, int nNodes)
{
  double xi  = in_rst.x;
  double eta = in_rst.y;
  double mu = in_rst.z;

  if (nNodes == 20) {
    /* Quadratic Serendiptiy Hex */
    double XI[8]  = {-1,1,1,-1,-1,1,1,-1};
    double ETA[8] = {-1,-1,1,1,-1,-1,1,1};
    double MU[8]  = {-1,-1,-1,-1,1,1,1,1};
    // Corner Nodes
    for (int i=0; i<8; i++) {
      out_dshape[3*i+0] = .125*XI[i] *(1+eta*ETA[i])*(1 + mu*MU[i])*(2*xi*XI[i] +   eta*ETA[i] +   mu*MU[i]-1);
      out_dshape[3*i+1] = .125*ETA[i]*(1 + xi*XI[i])*(1 + mu*MU[i])*(  xi*XI[i] + 2*eta*ETA[i] +   mu*MU[i]-1);
      out_dshape[3*i+2] = .125*MU[i] *(1 + xi*XI[i])*(1+eta*ETA[i])*(  xi*XI[i] +   eta*ETA[i] + 2*mu*MU[i]-1);
    }
    // Edge Nodes, xi = 0
    out_dshape[ 3*8+0] = -.5*xi*(1-eta)*(1-mu);  out_dshape[ 3*8+1] = -.25*(1-xi*xi)*(1-mu);  out_dshape[ 3*8+2] = -.25*(1-xi*xi)*(1-eta);
    out_dshape[3*10+0] = -.5*xi*(1+eta)*(1-mu);  out_dshape[3*10+1] =  .25*(1-xi*xi)*(1-mu);  out_dshape[3*10+2] = -.25*(1-xi*xi)*(1+eta);
    out_dshape[3*16+0] = -.5*xi*(1-eta)*(1+mu);  out_dshape[3*16+1] = -.25*(1-xi*xi)*(1+mu);  out_dshape[3*16+2] =  .25*(1-xi*xi)*(1-eta);
    out_dshape[3*18+0] = -.5*xi*(1+eta)*(1+mu);  out_dshape[3*18+1] =  .25*(1-xi*xi)*(1+mu);  out_dshape[3*18+2] =  .25*(1-xi*xi)*(1+eta);
    // Edge Nodes, eta = 0
    out_dshape[ 3*9+1] = -.5*eta*(1+xi)*(1-mu);  out_dshape[ 3*9+0] =  .25*(1-eta*eta)*(1-mu);  out_dshape[ 3*9+2] = -.25*(1-eta*eta)*(1+xi);
    out_dshape[3*11+1] = -.5*eta*(1-xi)*(1-mu);  out_dshape[3*11+0] = -.25*(1-eta*eta)*(1-mu);  out_dshape[3*11+2] = -.25*(1-eta*eta)*(1-xi);
    out_dshape[3*17+1] = -.5*eta*(1+xi)*(1+mu);  out_dshape[3*17+0] =  .25*(1-eta*eta)*(1+mu);  out_dshape[3*17+2] =  .25*(1-eta*eta)*(1+xi);
    out_dshape[3*19+1] = -.5*eta*(1-xi)*(1+mu);  out_dshape[3*19+0] = -.25*(1-eta*eta)*(1+mu);  out_dshape[3*19+2] =  .25*(1-eta*eta)*(1-xi);
    // Edge Nodes, mu = 0;
    out_dshape[3*12+2] = -.5*mu*(1-xi)*(1-eta);  out_dshape[3*12+0] = -.25*(1-mu*mu)*(1-eta);  out_dshape[3*12+1] = -.25*(1-mu*mu)*(1-xi);
    out_dshape[3*13+2] = -.5*mu*(1+xi)*(1-eta);  out_dshape[3*13+0] =  .25*(1-mu*mu)*(1-eta);  out_dshape[3*13+1] = -.25*(1-mu*mu)*(1+xi);
    out_dshape[3*14+2] = -.5*mu*(1+xi)*(1+eta);  out_dshape[3*14+0] =  .25*(1-mu*mu)*(1+eta);  out_dshape[3*14+1] =  .25*(1-mu*mu)*(1+xi);
    out_dshape[3*15+2] = -.5*mu*(1-xi)*(1+eta);  out_dshape[3*15+0] = -.25*(1-mu*mu)*(1+eta);  out_dshape[3*15+1] =  .25*(1-mu*mu)*(1-xi);
  }
  else {
    int nSide = cbrt(nNodes);

    if (nSide*nSide*nSide != nNodes)
      ThrowException("For Lagrange hex of order N, must have (N+1)^3 shape points.");

    if (xlist.size() != nSide)
    {
      xlist.resize(nSide);
      double dxi = 2./(nSide-1);

      for (int i=0; i<nSide; i++)
        xlist[i] = -1. + i*dxi;
    }

    if (ijk2gmsh.size() != nNodes)
      ijk2gmsh = structured_to_gmsh_hex(nNodes);

    // Pre-compute Lagrange function values to save redundant calculations
    lag_i.resize(nSide);
    lag_j.resize(nSide);
    lag_k.resize(nSide);
    dlag_i.resize(nSide);
    dlag_j.resize(nSide);
    dlag_k.resize(nSide);

#pragma omp parallel for
    for (int i = 0; i < nSide; i++)
    {
      lag_i[i] = Lagrange(xlist,  xi, i);
      lag_j[i] = Lagrange(xlist, eta, i);
      lag_k[i] = Lagrange(xlist,  mu, i);
      dlag_i[i] = dLagrange(xlist,  xi, i);
      dlag_j[i] = dLagrange(xlist, eta, i);
      dlag_k[i] = dLagrange(xlist,  mu, i);
    }

#pragma omp parallel for collapse(3)
    for (int k = 0; k < nSide; k++)
    {
      for (int j = 0; j < nSide; j++)
      {
        for (int i = 0; i < nSide; i++)
        {
          int pt = i + nSide * (j + nSide * k);
          out_dshape[3*ijk2gmsh[pt]+0] = dlag_i[i] *  lag_j[j] *  lag_k[k];
          out_dshape[3*ijk2gmsh[pt]+1] =  lag_i[i] * dlag_j[j] *  lag_k[k];
          out_dshape[3*ijk2gmsh[pt]+2] =  lag_i[i] *  lag_j[j] * dlag_k[k];
        }
      }
    }
  }
}

void getSimplex(int nDims, const std::vector<double> &x0, double L, std::vector<double> &X)
{
  int nPts = nDims+1;
  double DOT = -1./nDims;

  // Storing each N-D point contiguously
  X.assign(nDims*nPts, 0);

  // Initialize the first dimension
  X[0] = 1.;

  for (int i = 0; i < nDims; i++)
  {
    // Calculate dot product with most recent point
    double dot = 0.;
    for (int k = 0; k < i; k++)
      dot += X[i*nDims+k] * X[i*nDims+k];

    // Ensure x_i dot x_j = -1/nDims
    dot = (DOT - dot) / X[i*nDims+i];
    for (int j = i+1; j < nPts; j++)
      X[j*nDims+i] = dot;

    // Ensure norm(x_i) = 1
    if (i+1 < nDims)
    {
      dot = 0.;
      for (int j = 0; j < i; j++)
        dot += X[(i+1)*nDims+j]*X[(i+1)*nDims+j];

      X[(i+1)*nDims+i+1] = std::sqrt(1.-std::abs(dot));
    }
  }

  // Scale and translate the final simplex
  for (int i = 0; i < nPts; i++)
  {
    for (int j = 0; j < nDims; j++)
    {
      X[i*nDims+j] *= L;
      X[i*nDims+j] += x0[j];
    }
  }
}

point CalcPos(std::vector<double> &shape, double* xv,
    const std::vector<double> &xloc, int nDims)
{
  point pt = point(&xloc[0],nDims);

  if (nDims == 3)
    shape_hex(pt, shape, shape.size());
  else
    shape_quad(pt, shape, shape.size());

  pt.zero();
  for (int n = 0; n < shape.size(); n++)
    for (int i = 0; i < 3; i++)
      pt[i] += shape[n] * xv[n*3+i];

  return pt;
}


point CalcPos1D(std::vector<double> &shape, double* xv,
    const std::vector<double> &xloc)
{
  shape_line(xloc[0], shape, shape.size());

  point pt;
  for (int n = 0; n < shape.size(); n++)
    for (int i = 0; i < 2; i++)
      pt[i] += shape[n] * xv[n*2+i];

  return pt;
}

point CalcPos2D(std::vector<double> &shape, double* xv,
    const std::vector<double> &xloc)
{
  point pt = point(&xloc[0],2);

  shape_quad(pt, shape, shape.size());

  pt.zero();
  for (int n = 0; n < shape.size(); n++)
    for (int i = 0; i < 2; i++)
      pt[i] += shape[n] * xv[n*2+i];

  return pt;
}

point CalcPos3D(std::vector<double> &shape, double* xv,
    const std::vector<double> &xloc)
{
  point pt = point(&xloc[0],2);

  shape_quad(pt, shape, shape.size());

  pt.zero();
  for (int n = 0; n < shape.size(); n++)
    for (int i = 0; i < 3; i++)
      pt[i] += shape[n] * xv[n*3+i];

  return pt;
}

double constraintFunc(const std::vector<double> &refLoc)
{
  double maxVal = 0;
  for (auto &coord : refLoc)
    maxVal = std::max(maxVal,std::abs(coord));

  if (maxVal > 1.)
    return maxVal;
  else
    return -1.;
}

Vec3 intersectionCheck(double *fxv, int nfv, double *exv, int nev, int nDims)
{
  std::vector<double> shapeC(nev), shapeF(nfv);

  int num_evals = 0;

  double eps = 2e-8;
  auto minFunc = [&](const std::vector<double> &X_IN) -> double
  {
    double rst[3];
    point pt = (nDims==2) ? CalcPos1D(shapeF, fxv, X_IN)
                          : CalcPos3D(shapeF, fxv, X_IN);
    double xyz[3] = {pt.x,pt.y,pt.z};
    getRefLocNewton(exv, xyz, rst, nev, nDims);

    double maxVal = 0;
    for (int i = 0; i < nDims; i++)
      maxVal = std::max(maxVal,std::abs(rst[i]));

    num_evals++;

    if (maxVal > 1. + eps)
      return maxVal - 1.;
    else
      return 0.;
  };

  NM_FVAL mini;
  if (nDims == 2)
    mini = NelderMead_constrained<1>({0}, minFunc, constraintFunc, .75);
  else
    mini = NelderMead_constrained<2>({0,0}, minFunc, constraintFunc, .3);

  if (mini.f < eps)
  {
    return Vec3(0,0,0);
  }
  else
  {
    double rst[3];
    point pt = (nDims==2) ? CalcPos1D(shapeF, fxv, mini.x)
                          : CalcPos3D(shapeF, fxv, mini.x);
    double xyz[3] = {pt.x,pt.y,pt.z};
    getRefLocNewton(exv, xyz, rst, nev, nDims);

    for (int i = 0; i < nDims; i++)
      rst[i] = std::min(std::max(rst[i], -1.), 1.);

    point ptC = CalcPos(shapeC, exv, {rst[0],rst[1],rst[2]}, 3);

    return ptC - pt;
  }
}

} // namespace tg_funcs
