// This file is part of the AliceVision project and is made available under
// the terms of the MPL2 license (see the COPYING.md file).

#include "aliceVision/geometry/HalfPlane.hpp"
#include "aliceVision/geometry/Frustum.hpp"

#include <iostream>

#define BOOST_TEST_MODULE halfSpaceIntersection
#include <boost/test/included/unit_test.hpp>
#include <boost/test/floating_point_comparison.hpp>

using namespace aliceVision;
using namespace aliceVision::geometry::halfPlane;
using namespace std;

BOOST_AUTO_TEST_CASE(ExistingSubspace) {

	std::vector<Half_plane> vec_hplanes;

	Vec3 a,b,c;
	a << 0,0,0;
	b << 1,0,0;
	c << 0,1,0;

	Vec3 offset(0,0,2);
	vec_hplanes.push_back(Half_plane_p(a,b,c));
	vec_hplanes.push_back(Half_plane_p(a+offset,b+offset,c+offset));

	//    /\
	// ___|____ z = 2
	//
	//    /\
	// ___|____ z = 0

  BOOST_CHECK(isNotEmpty(vec_hplanes));
}

BOOST_AUTO_TEST_CASE(EmptyIntersection) {

	std::vector<Half_plane> vec_hplanes;

	Vec3 a,b,c;
	a << 0,0,0;
	b << 1,0,0;
	c << 0,1,0;

	Vec3 offset(0,0,2);
	vec_hplanes.push_back(Half_plane_p(a,b,c));
	vec_hplanes.push_back(Half_plane_p(a+offset,b+offset,c+offset));
	vec_hplanes[1].normal() *= -1; //invert the side of the half plane

	//    /\
	// ___|____ z = 0
	//
	//
	// _______ z = -2
	//    |
	//   \/

  BOOST_CHECK(!isNotEmpty(vec_hplanes));
}
