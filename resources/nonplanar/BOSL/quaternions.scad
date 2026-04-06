///////////////////////////////////////////
// LibFile: quaternions.scad
//   Support for Quaternions.
//   To use, add the following line to the beginning of your file:
//   ```
//   use <BOSL/quaternions.scad>
//   ```
///////////////////////////////////////////

/*
BSD 2-Clause License

Copyright (c) 2017, Revar Desmera
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


use <math.scad>


// Section: Quaternions
//   Quaternions are fast methods of storing and calculating arbitrary rotations.
//   Quaternions contain information on both axis of rotation, and rotation angle.
//   You can chain multiple rotation together by multiplying quaternions together.
//   They don't suffer from the gimbal-lock issues that [X,Y,Z] rotation angles do.
//   Quaternions are stored internally as a 4-value vector:
//   `[X, Y, Z, W]  =  W + Xi + Yj + Zk`


// Internal
function _Quat(a,s,w) = [a[0]*s, a[1]*s, a[2]*s, w];


// Function: Quat()
// Usage:
//   Quat(ax, ang);
// Description: Create a new Quaternion from axis and angle of rotation.
// Arguments:
//   ax = Vector of axis of rotation.
//   ang = Number of degrees to rotate around the axis counter-clockwise, when facing the origin.
function Quat(ax=[0,0,1], ang=0) = _Quat(ax/norm(ax), sin(ang/2), cos(ang/2));


// Function: QuatX()
// Usage:
//   QuatX(a);
// Description: Create a new Quaternion for rotating around the X axis [1,0,0].
// Arguments:
//   a = Number of degrees to rotate around the axis counter-clockwise, when facing the origin.
function QuatX(a=0) = Quat([1,0,0],a);


// Function: QuatY()
// Usage:
//   QuatY(a);
// Description: Create a new Quaternion for rotating around the Y axis [0,1,0].
// Arguments:
//   a = Number of degrees to rotate around the axis counter-clockwise, when facing the origin.
function QuatY(a=0) = Quat([0,1,0],a);

// Function: QuatZ()
// Usage:
//   QuatZ(a);
// Description: Create a new Quaternion for rotating around the Z axis [0,0,1].
// Arguments:
//   a = Number of degrees to rotate around the axis counter-clockwise, when facing the origin.
function QuatZ(a=0) = Quat([0,0,1],a);


// Function: QuatXYZ()
// Usage:
//   QuatXYZ([X,Y,Z])
// Description:
//   Creates a quaternion from standard [X,Y,Z] rotation angles in degrees.
// Arguments:
//   a = The triplet of rotation angles, [X,Y,Z]
function QuatXYZ(a=[0,0,0]) =
	let(
		qx = QuatX(a[0]),
		qy = QuatY(a[1]),
		qz = QuatZ(a[2])
	)
	Q_Mul(qz, Q_Mul(qy, qx));


// Function: Q_Ident()
// Description: Returns the "Identity" zero-rotation Quaternion.
function Q_Ident() = [0, 0, 0, 1];


// Function: Q_Add_S()
// Usage:
//   Q_Add_S(q, s)
// Description: Adds a scalar value `s` to the W part of a quaternion `q`.
function Q_Add_S(q, s) = q+[0,0,0,s];


// Function: Q_Sub_S()
// Usage:
//   Q_Sub_S(q, s)
// Description: Subtracts a scalar value `s` from the W part of a quaternion `q`.
function Q_Sub_S(q, s) = q-[0,0,0,s];


// Function: Q_Mul_S()
// Usage:
//   Q_Mul_S(q, s)
// Description: Multiplies each part of a quaternion `q` by a scalar value `s`.
function Q_Mul_S(q, s) = q*s;


// Function: Q_Div_S()
// Usage:
//   Q_Div_S(q, s)
// Description: Divides each part of a quaternion `q` by a scalar value `s`.
function Q_Div_S(q, s) = q/s;


// Function: Q_Add()
// Usage:
//   Q_Add(a, b)
// Description: Adds each part of two quaternions together.
function Q_Add(a, b) = a+b;


// Function: Q_Sub()
// Usage:
//   Q_Sub(a, b)
// Description: Subtracts each part of quaternion `b` from quaternion `a`.
function Q_Sub(a, b) = a-b;


// Function: Q_Mul()
// Usage:
//   Q_Mul(a, b)
// Description: Multiplies quaternion `a` by quaternion `b`.
function Q_Mul(a, b) = [
	a[3]*b.x  + a.x*b[3] + a.y*b.z  - a.z*b.y,
	a[3]*b.y  - a.x*b.z  + a.y*b[3] + a.z*b.x,
	a[3]*b.z  + a.x*b.y  - a.y*b.x  + a.z*b[3],
	a[3]*b[3] - a.x*b.x  - a.y*b.y  - a.z*b.z,
];


// Function: Q_Dot()
// Usage:
//   Q_Dot(a, b)
// Description: Calculates the dot product between quaternions `a` and `b`.
function Q_Dot(a, b) = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];


// Function: Q_Neg()
// Usage:
//   Q_Neg(q)
// Description: Returns the negative of quaternion `q`.
function Q_Neg(q) = -q;


// Function: Q_Conj()
// Usage:
//   Q_Conj(q)
// Description: Returns the conjugate of quaternion `q`.
function Q_Conj(q) = [-q.x, -q.y, -q.z, q[3]];


// Function: Q_Norm()
// Usage:
//   Q_Norm(q)
// Description: Returns the `norm()` "length" of quaternion `q`.
function Q_Norm(q) = norm(q);


// Function: Q_Normalize()
// Usage:
//   Q_Normalize(q)
// Description: Normalizes quaternion `q`, so that norm([W,X,Y,Z]) == 1.
function Q_Normalize(q) = q/norm(q);


// Function: Q_Dist()
// Usage:
//   Q_Dist(q1, q2)
// Description: Returns the "distance" between two quaternions.
function Q_Dist(q1, q2) = norm(q2-q1);


// Function: Q_Slerp()
// Usage:
//   Q_Slerp(q1, q2, u);
// Description:
//   Returns a quaternion that is a spherical interpolation between two quaternions.
// Arguments:
//   q1 = The first quaternion. (u=0)
//   q2 = The second quaternion. (u=1)
//   u = The proportional value, from 0 to 1, of what part of the interpolation to return.
// Example(3D):
//   a = QuatY(15);
//   b = QuatY(75);
//   color("blue",0.25) Qrot(a) cylinder(d=1, h=80);
//   color("red",0.25) Qrot(b) cylinder(d=1, h=80);
//   Qrot(Q_Slerp(a, b, 0.6)) cylinder(d=1, h=80);
function Q_Slerp(q1, q2, u) = let(
		dot = Q_Dot(q1, q2),
		qq2 = dot<0? Q_Neg(q2) : q2,
		dott = dot<0? -dot : dot,
		theta = u * acos(constrain(dott,-1,1))
	) (dott>0.9995)?
		Q_Normalize(q1 + ((qq2-q1) * u)) :
		(q1*cos(theta) + (Q_Normalize(qq2 - (q1 * dott)) * sin(theta)));


// Function: Q_Matrix3()
// Usage:
//   Q_Matrix3(q);
// Description:
//   Returns the 3x3 rotation matrix for the given normalized quaternion q.
function Q_Matrix3(q) = [
	[1-2*q[1]*q[1]-2*q[2]*q[2],   2*q[0]*q[1]-2*q[2]*q[3],   2*q[0]*q[2]+2*q[1]*q[3]],
	[  2*q[0]*q[1]+2*q[2]*q[3], 1-2*q[0]*q[0]-2*q[2]*q[2],   2*q[1]*q[2]-2*q[0]*q[3]],
	[  2*q[0]*q[2]-2*q[1]*q[3],   2*q[1]*q[2]+2*q[0]*q[3], 1-2*q[0]*q[0]-2*q[1]*q[1]]
];


// Function: Q_Matrix4()
// Usage:
//   Q_Matrix4(q);
// Description:
//   Returns the 4x4 rotation matrix for the given normalized quaternion q.
function Q_Matrix4(q) = [
	[1-2*q[1]*q[1]-2*q[2]*q[2],   2*q[0]*q[1]-2*q[2]*q[3],   2*q[0]*q[2]+2*q[1]*q[3], 0],
	[  2*q[0]*q[1]+2*q[2]*q[3], 1-2*q[0]*q[0]-2*q[2]*q[2],   2*q[1]*q[2]-2*q[0]*q[3], 0],
	[  2*q[0]*q[2]-2*q[1]*q[3],   2*q[1]*q[2]+2*q[0]*q[3], 1-2*q[0]*q[0]-2*q[1]*q[1], 0],
	[                        0,                         0,                         0, 1]
];


// Function: Q_Axis()
// Usage:
//   Q_Axis(q)
// Description:
//   Returns the axis of rotation of a normalized quaternion `q`.
function Q_Axis(q) = let(d = sqrt(1-(q[3]*q[3]))) (d==0)? [0,0,1] : [q[0]/d, q[1]/d, q[2]/d];


// Function: Q_Angle()
// Usage:
//   Q_Angle(q)
// Description:
// Returns the angle of rotation (in degrees) of a normalized quaternion `q`.
function Q_Angle(q) = 2 * acos(q[3]);


// Function: Q_Rot_Vector()
// Usage:
//   Q_Rot_Vector(v,q);
// Description:
//   Returns the vector `v` after rotating it by the quaternion `q`.
function Q_Rot_Vector(v,q) = Q_Mul(Q_Mul(q,concat(v,0)),Q_Conj(q));


// Module: Qrot()
// Usage:
//   Qrot(q) ...
// Description:
//   Rotate all children by the rotation stored in quaternion `q`.
// Example(FlatSpin):
//   q = QuatXYZ([45,35,10]);
//   color("red",0.25) cylinder(d=1,h=80);
//   Qrot(q) cylinder(d=1,h=80);
module Qrot(q) {
	multmatrix(Q_Matrix4(q)) {
		children();
	}
}


// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
