#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>

namespace vrt {

struct Pixel {
	float r, g, b, a;
	Pixel() : r(0), g(0), b(0) {}
	Pixel(float a) : r(a), g(a), b(a), a(a) {}
	Pixel(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
};

class Image {
	std::vector<Pixel> pixels;
	unsigned w, h;

public:
	Image(unsigned w, unsigned h);
	Image(unsigned w, unsigned h, std::vector<Pixel> const& pixels);

	unsigned width() const;
	unsigned height() const;
	auto const& getPixels() const;
	void put(unsigned x, unsigned y, Pixel const& p);
	void put(unsigned x, Pixel const& p);
};

bool savePPMImage(Image const& image, std::string const& path);
}
