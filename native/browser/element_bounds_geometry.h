// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cmath>
namespace suitecut {
// Project actual Inspector viewport coordinates through the real parent frame
// content quad. All four selected-node corners use one captured frame transform.
// No DOM rectangle, browser identity or missing point is synthesized here.
inline bool MapElementBoundsQuad(const std::array<double,8>& points,
                                 const std::array<double,8>& content,
                                 double width,double height,
                                 std::array<double,8>& mapped){
  if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return false;
  for(const auto value:points)if(!std::isfinite(value))return false;
  for(const auto value:content)if(!std::isfinite(value))return false;
  const auto& q=content;
  const double dx1=q[2]-q[4],dx2=q[6]-q[4],dx3=q[0]-q[2]+q[4]-q[6];
  const double dy1=q[3]-q[5],dy2=q[7]-q[5],dy3=q[1]-q[3]+q[5]-q[7];
  const double determinant=dx1*dy2-dx2*dy1;
  double g=0,h=0;
  if(std::abs(dx3)>1e-9||std::abs(dy3)>1e-9){
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return false;
    g=(dx3*dy2-dx2*dy3)/determinant;h=(dx1*dy3-dx3*dy1)/determinant;
  }
  // Collinear content corners cannot describe a visible frame viewport.
  const double area=(q[2]-q[0])*(q[7]-q[1])-(q[3]-q[1])*(q[6]-q[0]);
  if(!std::isfinite(area)||std::abs(area)<1e-12)return false;
  std::array<double,8> result{};
  for(std::size_t i=0;i<4;++i){
    const double u=points[2*i]/width,v=points[2*i+1]/height,divisor=g*u+h*v+1;
    if(!std::isfinite(divisor)||std::abs(divisor)<1e-12)return false;
    result[2*i]=((q[2]-q[0]+g*q[2])*u+(q[6]-q[0]+h*q[6])*v+q[0])/divisor;
    result[2*i+1]=((q[3]-q[1]+g*q[3])*u+(q[7]-q[1]+h*q[7])*v+q[1])/divisor;
    if(!std::isfinite(result[2*i])||!std::isfinite(result[2*i+1]))return false;
  }
  mapped=result;return true;
}
} // namespace suitecut
