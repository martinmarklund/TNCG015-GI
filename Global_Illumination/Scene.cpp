#include "Scene.h"

#include <glm/gtx/rotate_vector.hpp> 

Scene::Scene() {
	printf("Creating room");
	createRoom();
}

Scene::~Scene()
{
	for (size_t i = 0; i < _objects.size(); i++)
	{
			delete _objects[i];
	}
}

// Loop over all objects in the scene and detect ray intersections
bool Scene::castRay(Ray& ray, size_t depth) {
	
	// Trace room
	for (Triangle triangle : _triangles) {
		triangle.rayIntersection(ray);
	}
	
	// Trace objects
	for (Object* object : _objects) {
		object->rayIntersection(ray);
	}

	// Trace light sources
	for (LightSource light : _lightSources) {
		light.rayIntersection(ray);
	}

	// Determine wether we hit something or not
	if (!ray.hasIntersection()) {
		return false;
	}
	else {

		// If depth is -1, then this is a shadow ray ==> return
		if (depth == -1) {
			return true;
		}
		// Else find out wether we should spawn a new ray or not
		else {
			switch (ray.getIntersectionMaterial()) {
				case LAMBERTIAN:
				{
				
					// Cast a shadow ray to calculate direct light contribution
					ColorDbl lightContribution = this->castShadowRay(ray.getIntersectionPoint(), ray.getIntersectionNormal());
					ray.updateIntersection(
						ray.getClosestIntersection(),
						ray.getIntersectionPoint(),
						ray.getColor() * lightContribution,
						ray.getIntersectionNormal(),
						ray.getIntersectionMaterial()
					);


					double  alpha = 0.25;
					// Russian roulette
					if (1 - alpha > randMinMax(0, 1)) {


						double xi = randMinMax(EPSILON, 2.0 * (double)M_PI - (double)EPSILON);  //uniformRand();
						double yj = randMinMax(EPSILON, 2.0 * (double)M_PI - (double)EPSILON);  //uniformRand();


						// The out vector can be found using spherical coordinats (r = 1)
						float altitude = 2.f * M_PI * xi;
						float azimuth = asin(sqrt(yj));

						float localX = cos(azimuth) * sin(altitude);
						float localY = sin(azimuth) * sin(altitude);
						float localZ = cos(altitude);

						Vertex localCoordinates(localX, localY, localZ, 1.0);

						// Calculate matrix that will transform our local spherical coordinates to global coordinates
						glm::vec4 Z(ray.getIntersectionNormal(), 0);
						glm::vec4 I(ray.getIntersectionPoint() - ray.getStart());
						glm::vec4 X(glm::normalize(I - (glm::dot(I, Z)) * Z));
						glm::vec4 Y(glm::cross(glm::vec3(-X), glm::vec3(Z)), 0);
						
						glm::vec4 modIP = glm::vec4(glm::vec3(-ray.getIntersectionPoint()), 1.0f);
						glm::mat4x4 M = glm::mat4(X, Y, Z, glm::vec4(0, 0, 0, 1)) *
							glm::mat4(glm::vec4(1, 0, 0, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 1, 0), modIP);
						
						glm::mat4x4 invM = glm::inverse(M);

						Vertex globalCoordinates = invM * localCoordinates;

						Ray reflectedRay(ray.getIntersectionPoint() + (globalCoordinates - ray.getIntersectionPoint()) * 0.001f, globalCoordinates);
						this->castRay(reflectedRay, depth + 1);

						const double REFLECTIVITY = 0.8 / ((double)M_PI * ((double)depth + 1.0));

						// Update ray color, should maybe be done in a cleaner fashion...
						ray.updateIntersection(
							ray.getClosestIntersection(),
							ray.getIntersectionPoint(),
							(ray.getColor() * (1 - REFLECTIVITY)) + (reflectedRay.getColor() * (REFLECTIVITY)),
							ray.getIntersectionNormal(),
							ray.getIntersectionMaterial()
						);
						;
					}

					break;
				}
				case PERFECT_REFLECTOR:
				{
					// Cast a shadow ray to calculate direct light contribution
					ColorDbl lightContribution =  this->castShadowRay(ray.getIntersectionPoint(), ray.getIntersectionNormal());

					if (depth < MAX_DEPTH) {
						// Perfect reflectors should always reflect a new ray as long as we haven't exceeded ray-threshold
						Direction inDirection = ray.getIntersectionPoint() - ray.getStart();
						Direction outDirection = glm::reflect(inDirection, ray.getIntersectionNormal());

						Ray reflectedRay(ray.getIntersectionPoint() + (Vertex(outDirection, 1.0) - ray.getIntersectionPoint()) * 0.001f, Vertex(outDirection, 1.0));

						this->castRay(reflectedRay, depth + 1);

						// Update ray color, should maybe be done in a cleaner fashion...
						ray.updateIntersection(
							ray.getClosestIntersection(),
							ray.getIntersectionPoint(),
							0.2 * lightContribution + 0.8 * reflectedRay.getColor(),
							ray.getIntersectionNormal(),
							ray.getIntersectionMaterial()
						);
					}
					else {
						ray.updateIntersection(
							ray.getClosestIntersection(),
							ray.getIntersectionPoint(),
							lightContribution,
							ray.getIntersectionNormal(),
							ray.getIntersectionMaterial()
						);
					}
					break;
				}
				case LIGHT_SOURCE:
				{
					const double LIGHT_EMISSION = 255;
					// Light sources should not spawn any new rays
					ray.updateIntersection(
						ray.getClosestIntersection(),
						ray.getIntersectionPoint(),
						ray.getColor()* LIGHT_EMISSION,
						ray.getIntersectionNormal(),
						ray.getIntersectionMaterial()
					);
					break;
				}
			}
		}

		return true;
	}
}

ColorDbl Scene::castShadowRay(const Vertex& origin, const Direction& normal) {
	// Initial light contribution is zero (since light contribtuion is applied additatively)
	ColorDbl lightContribution(0.0);
	
	int lightCount = 0;			// Numer of light sources / light triangles we cast shadow rays towards
	double lightArea = 0.0;		// Area of light sources taken into account

	for(LightSource light : _lightSources) {
		for (Triangle lightTriangle : light.getTriangles()) {
			lightArea += lightTriangle.getArea();	
			/* Each iteration of the loop below will change the random point on the light source
			 * which might add to the light contribution if the origin can only partially see the
			 * light source. Thus, a higher shadow ray count should increase the brightness in the scene */ 
			for (int i = 0; i < SHADOW_RAY_COUNT; i++) {
				++lightCount;

				Vertex lightPoint = lightTriangle.getRandomPoint();

				// Create a shadow ray towards our light source
				Ray shadowRay(origin + (Vertex(normal, 0.0) * 0.1f), lightPoint);
				this->castRay(shadowRay,  -1); // Depth -1 is reserved for shadow rays to avoid spawning new rays

				// Does the shadow ray have a direct line of sight to the point on the light source?
				if (shadowRay.hasIntersection() && shadowRay.getIntersectionMaterial() == LIGHT_SOURCE) {
					Direction lightNormal = shadowRay.getIntersectionNormal();
					Direction shadowRayDir = glm::normalize(shadowRay.getEnd() - shadowRay.getStart());

					// Since both vectors are normalized, the dot product returns the cosine between the two vectors
					// Negative means that the shadow ray and light source are facing eachother which means that we still want that sweet contribution
					double dt = glm::dot(shadowRayDir, lightNormal);
					if(dt < 0 ) {
						dt *= -1;
					}

					lightContribution += shadowRay.getColor() * dt;
				}
			}
		}
	}
	// lightCount takes into account for how many shadow rays we have used for each lightsource
	return lightContribution / (double)lightCount;
}

void Scene::addLightsource(LightSource& lightsource) {
	_lightSources.push_back(lightsource);
}

void Scene::addObject(Object* object) 
{
	_objects.push_back(object);
}

void Scene::createRoom() {
	_vertices =
	{
		Vertex(0.0f, 6.0f, 5.0f, 1.0f),		//  (0)		d-top
		Vertex(0.0f, 6.0f, -5.0f, 1.0f),	//	(1)		d-bottom
		Vertex(10.0f, 6.0f, 5.0f, 1.0f),	//	(2)		b-top
		Vertex(10.0f, 6.0f, -5.0f, 1.0f),	//	(3)		b-bottom
		Vertex(13.0f, 0.0f, 5.0f, 1.0f),	//	(4)		a-top
		Vertex(13.0f, 0.0f, -5.0f, 1.0f),	//	(5)		a-bottom
		Vertex(10.0f, -6.0f, 5.0f, 1.0f),	//	(6)		c-top
		Vertex(10.0f, -6.0f, -5.0f, 1.0f),	//	(7)		c-bottom
		Vertex(0.0f, -6.0f, 5.0f, 1.0f),	//	(8)		e-top
		Vertex(0.0f, -6.0f, -5.0f, 1.0f),	//	(9)		e-bottom
		Vertex(-3.0f, 0.0f, 5.0f, 1.0f),	//	(10)	f-top
		Vertex(-3.0f, 0.0f, -5.0f, 1.0f),	//	(11)	f-bottom
		Vertex(5.0f, 0.0f, 5.0f, 1.0f),		//	(12)
		Vertex(5.0f, 0.0f, -5.0f, 1.0f)		//	(13)
	};

	// The scene viewed from above:
	// ^ = Camera 1 and its viewing direction (origo)
	// v = camera 2 and its viewing direction
	//           a
	//        /     \    <--- FRONT
	//LEFT	b    v    c  RIGHT
	//      |         |  <--- CENTER
	//      d    ^    e
	//        \     /    <--- BACK
	//			 f
	//           
	//			 ^ x-direction
	//           | 
	//       <---|  y-direction
	//		z-direction UPWARDS
	
	const ColorDbl Red = ColorDbl(240, 0, 0);
	const ColorDbl Green = ColorDbl(0, 240, 0);
	const ColorDbl Blue = ColorDbl(0, 0, 240);
	const ColorDbl White = ColorDbl(255, 255, 255);

	//Floor (normals point up)
	_triangles.push_back(Triangle(_vertices.at(3), _vertices.at(7), _vertices.at(5), White));
	_triangles.push_back(Triangle(_vertices.at(3), _vertices.at(1), _vertices.at(7), White));
	_triangles.push_back(Triangle(_vertices.at(1), _vertices.at(9), _vertices.at(7), White));
	_triangles.push_back(Triangle(_vertices.at(1), _vertices.at(11), _vertices.at(9), White));
	//Roof (normals point down)
	_triangles.push_back(Triangle(_vertices.at(2), _vertices.at(4), _vertices.at(6), White));
	_triangles.push_back(Triangle(_vertices.at(2), _vertices.at(6), _vertices.at(0), White));
	_triangles.push_back(Triangle(_vertices.at(0), _vertices.at(6), _vertices.at(8), White));
	_triangles.push_back(Triangle(_vertices.at(0), _vertices.at(8), _vertices.at(10), White));
	//Left middle (normals point right)
	_triangles.push_back(Triangle(_vertices.at(3), _vertices.at(2), _vertices.at(0), Green));
	_triangles.push_back(Triangle(_vertices.at(3), _vertices.at(0), _vertices.at(1), Green));
	//Left front (normals point right)
	_triangles.push_back(Triangle(_vertices.at(5), _vertices.at(4), _vertices.at(2), Green));
	_triangles.push_back(Triangle(_vertices.at(5), _vertices.at(2), _vertices.at(3), Green));
	//Left back (normals point right)
	_triangles.push_back(Triangle(_vertices.at(1), _vertices.at(0), _vertices.at(10), Green));
	_triangles.push_back(Triangle(_vertices.at(1), _vertices.at(10), _vertices.at(11), Green));
	//Right middle (normals pointing left)
	_triangles.push_back(Triangle(_vertices.at(9), _vertices.at(8), _vertices.at(6), Blue));
	_triangles.push_back(Triangle(_vertices.at(9), _vertices.at(6), _vertices.at(7), Blue));
	//Right front (normals pointing left)
	_triangles.push_back(Triangle(_vertices.at(7), _vertices.at(6), _vertices.at(4), Blue));
	_triangles.push_back(Triangle(_vertices.at(7), _vertices.at(4), _vertices.at(5), Blue));
	//Right back (normals pointing left)
	_triangles.push_back(Triangle(_vertices.at(11), _vertices.at(10), _vertices.at(8), Red));
	_triangles.push_back(Triangle(_vertices.at(11), _vertices.at(8), _vertices.at(9), Red));

	// Set the material of all triangles making up the room to diffuse
	for (size_t i = 0; i < _triangles.size(); i++) {
		//if (i < 4)
		//	_triangles[i].updateMaterial(1);
		//else
			_triangles[i].updateMaterial(0);
	}
}