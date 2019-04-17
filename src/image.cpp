#include <image.hpp>

namespace vrt {

Image::Image(unsigned w, unsigned h) : w(w), h(h)
{
	pixels.resize(w*h);
}

Image::Image(unsigned w, unsigned h, std::vector<Pixel> const& pixels) : 
			w(w), h(h), pixels(pixels)
{
}

unsigned Image::width() const 
{
	return w;
}

unsigned Image::height() const 
{
	return h;
}

auto const& Image::getPixels() const 
{
	return pixels;
}

void Image::put(unsigned x, unsigned y, Pixel const& p)
{
	if (x < w && y < h) {
		pixels[x + y * w] = p;
	} else {
		std::cerr << "Failed to put pixel"
					<< std::endl;
		return;
	}
}

void Image::put(unsigned x, Pixel const& p)
{
	if (x < w * h) {
		pixels[x] = p;
	} else {
		std::cerr << "Failed to put pixel"
					<< std::endl;
		return;
	}
}

bool savePPMImage(Image const& image, std::string const& path)
{
	std::ofstream fs(path);

	if (!fs.is_open()) {
		std::cerr << "Failed to save image to: "
					<< path << std::endl;

		return false;
	}

	fs << "P3\n"
	   << image.width() << " "
	   << image.height() << "\n"
		<< "255\n";

	for (auto& p : image.getPixels()) {
		fs << static_cast<uint32_t>(std::fmin((p.r * 255), 255)) << " "
		   << static_cast<uint32_t>(std::fmin((p.g * 255), 255)) << " "
		   << static_cast<uint32_t>(std::fmin((p.b * 255), 255)) << " ";
	}

	return true;
}

}