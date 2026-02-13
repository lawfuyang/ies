#include "ies_Loader.h"
#include "rgbe.h"

#include <io.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <cmath>

struct IESOutputData
{
	std::uint32_t width;
	std::uint32_t height;
	std::uint32_t channel;
	std::vector<float> stream;
};

bool IES2HDR(const std::string& path, const std::string& outpath, IESFileInfo& info)
{
	std::fstream stream(path, std::ios_base::binary | std::ios_base::in);
	if (!stream.is_open())
	{
		std::cout << "IES2HDR Error: Failed to open file :" << path << std::endl;
		return false;
	}

	stream.seekg(0, std::ios_base::end);
	std::streamsize streamSize = stream.tellg();
	stream.seekg(0, std::ios_base::beg);

	std::vector<std::uint8_t> IESBuffer;
	IESBuffer.resize(streamSize + 1);
	stream.read((char*)IESBuffer.data(), IESBuffer.size());

	IESLoadHelper IESLoader;
	if (!IESLoader.load((char*)IESBuffer.data(), streamSize, info))
		return false;

	IESOutputData HDRdata;
	HDRdata.width = 128;
	HDRdata.height = 128;
	HDRdata.channel = 3;
	HDRdata.stream.resize(HDRdata.width * HDRdata.height * HDRdata.channel);

	// Determine whether to save as 1D or 2D based on horizontal angle variations
	bool use1D = false;

	if (info.anglesNumH <= 1)
	{
		// Only 1 horizontal angle, use 1D
		use1D = true;

		std::cout << "IES2HDR Info: Only 1 horizontal angle detected, saving as 1D HDR." << std::endl;
	}
	else
	{
		// Multiple horizontal angles - check for minimal variation
		// We'll use a liberal threshold: if variation is minimal, still use 1D
		
		// Compute coefficient of variation across horizontal angles
		// For each vertical angle, compare the candela values across all horizontal angles
		double avgVariation = 0.0;
		int verticalAngles = info.anglesNumV;
		int horizontalAngles = info.anglesNumH;
		const auto& candalaValues = info.getCandalaValues();

		for (int v = 0; v < verticalAngles; ++v)
		{
			// Get candela values for this vertical angle across all horizontal angles
			float minVal = std::numeric_limits<float>::max();
			float maxVal = std::numeric_limits<float>::lowest();
			double sum = 0.0;
			double sumSq = 0.0;

			for (int h = 0; h < horizontalAngles; ++h)
			{
				// candalaValues is organized as: [H0V0, H0V1, ..., H0V(n-1), H1V0, ...]
				float val = candalaValues[h * verticalAngles + v];
				
				minVal = std::min(minVal, val);
				maxVal = std::max(maxVal, val);
				sum += val;
				sumSq += val * val;
			}

			// Compute coefficient of variation for this vertical angle
			if (sum > 0.0)
			{
				double mean = sum / horizontalAngles;
				double variance = (sumSq / horizontalAngles) - (mean * mean);
				double stdDev = std::sqrt(std::max(0.0, variance));
				double coeffVar = stdDev / mean;
				avgVariation += coeffVar;

				// Also check max/min ratio as an alternative
				if (minVal > 0.0f && maxVal / minVal > 1.2f)
					avgVariation += 0.5; // Penalize large max/min ratio
			}
		}

		avgVariation /= verticalAngles;

		const double kVariationThreshold = 0.15;

		// Liberal threshold: use 1D if average variation is small
		use1D = (avgVariation < kVariationThreshold);

		std::cout << "IES2HDR Info: Average horizontal variation coefficient: " << avgVariation << std::endl;
		std::cout << "IES2HDR Info: " << (use1D ? "Minimal" : "Significant") << " horizontal variation detected, saving as " << (use1D ? "1D" : "2D") << " HDR." << std::endl;
	}

	if (use1D)
	{
		if (!IESLoader.saveAs1D(info, HDRdata.stream.data(), HDRdata.width, HDRdata.channel))
			return false;
	}
	else
	{
		if (!IESLoader.saveAs2D(info, HDRdata.stream.data(), HDRdata.width, HDRdata.height, HDRdata.channel))
			return false;
	}

	FILE* fp = std::fopen(outpath.c_str(), "wb");
	if (!fp)
	{
		std::cout << "IES2HDR Error: Failed to create file : " << outpath << path << std::endl;;
		return false;
	}

	rgbe_header_info hdr;
	hdr.valid = true;
	hdr.gamma = 1.0;
	hdr.exposure = 1.0;
	std::memcpy(hdr.programtype, "RADIANCE", 9);

	RGBE_WriteHeader(fp, HDRdata.width, HDRdata.height, &hdr);
	RGBE_WritePixels_RLE(fp, HDRdata.stream.data(), HDRdata.width, HDRdata.height);
	std::fclose(fp);

	return true;
}

bool IES2HDR(const std::string& path, IESFileInfo& info)
{
	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];

	std::memset(drive, 0, sizeof(drive));
	std::memset(dir, 0, sizeof(dir));
	std::memset(fname, 0, sizeof(fname));
	std::memset(ext, 0, sizeof(ext));

	_splitpath(path.c_str(), drive, dir, fname, ext);

	std::string out = drive;
	out += dir;
	out += fname;
	out += ".hdr";

	return IES2HDR(path, out, info);
}

int main(int argc, const char* argv[])
{
	try
	{
		if (argc <= 1)
		{
			std::cout << "IES2HDR Info: ERROR! Please enter a path to a IES file/directory." << std::endl;
			std::system("pause");
			return EXIT_FAILURE;
		}

		char drive[_MAX_DRIVE];
		char dir[_MAX_DIR];
		char fname[_MAX_FNAME];
		char ext[_MAX_EXT];

		std::memset(drive, 0, sizeof(drive));
		std::memset(dir, 0, sizeof(dir));
		std::memset(fname, 0, sizeof(fname));
		std::memset(ext, 0, sizeof(ext));

		_splitpath(argv[1], drive, dir, fname, ext);

		if (_stricmp(ext, ".ies") == 0)
		{
			IESFileInfo info;
			if (!IES2HDR(argv[1], info))
				std::cout << "IES2HDR Error: " << info.error() << std::endl;
		}
		else
		{
			std::string path = drive;
			path += dir;
			path += fname;
			path += "/*.ies";

			_finddata_t fileinfo;
			std::memset(&fileinfo, 0, sizeof(fileinfo));

			auto handle = _findfirst(path.data(), &fileinfo);
			if (handle != -1)
			{
				do
				{
					if (!(fileinfo.attrib & _A_SUBDIR))
					{
						std::string filename;
						filename = drive;
						filename += dir;
						filename += fname;
						filename += "/";
						filename += fileinfo.name;

						std::cout << "IES2HDR Info: Converting IES to HDR :" << filename << std::endl;

						IESFileInfo info;
						if (!IES2HDR(filename, info))
							std::cout << "IES2HDR Error: " << info.error() << std::endl;
					}
				} while (_findnext(handle, &fileinfo) == 0);

				_findclose(handle);
			}
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}