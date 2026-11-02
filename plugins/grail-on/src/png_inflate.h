// A small PNG reader for the plugin's own art (8-bit RGBA or RGB, not
// interlaced): zlib inflate (RFC 1951) and the PNG row filters. The art is
// compiled into the DLL's code section, so no resources and no Windows image
// codecs are used (antivirus scanners took the resource + WIC decoding for a
// dropper).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace PngLite {

struct Bits {
	const std::uint8_t* data;
	std::size_t         size, at = 0;
	std::uint32_t       buffer = 0;
	int                 count  = 0;
	bool                bad    = false;
	std::uint32_t get(int n) {
		while (count < n) {
			if (at >= size) {
				bad = true;
				return 0;
			}
			buffer |= static_cast<std::uint32_t>(data[at++]) << count;
			count += 8;
		}
		const std::uint32_t v = buffer & ((1u << n) - 1);
		buffer >>= n;
		count -= n;
		return v;
	}
};

struct Huffman {
	std::uint16_t counts[16] {};
	std::uint16_t symbols[320] {};
	void build(const std::uint8_t* lengths, int n) {
		std::memset(counts, 0, sizeof(counts));
		for (int i = 0; i < n; ++i) ++counts[lengths[i]];
		counts[0] = 0;
		std::uint16_t offs[16] {};
		for (int i = 1; i < 16; ++i) offs[i] = offs[i - 1] + counts[i - 1];
		for (int i = 0; i < n; ++i)
			if (lengths[i]) symbols[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
	}
	int decode(Bits& in) const {
		int code = 0, first = 0, index = 0;
		for (int len = 1; len < 16; ++len) {
			code |= static_cast<int>(in.get(1));
			const int count = counts[len];
			if (code - count < first) return symbols[index + (code - first)];
			index += count;
			first += count;
			first <<= 1;
			code <<= 1;
			if (in.bad) return -1;
		}
		return -1;
	}
};

inline bool Inflate(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out) {
	static const std::uint16_t lenBase[] { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const std::uint8_t  lenExtra[] { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static const std::uint16_t distBase[] { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	static const std::uint8_t  distExtra[] { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
	static const std::uint8_t  order[] { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	if (size < 2) return false;
	Bits in { data + 2, size - 2 }; // past the zlib header
	for (bool last = false; !last;) {
		last           = in.get(1) != 0;
		const int type = static_cast<int>(in.get(2));
		if (type == 0) {
			in.buffer = 0, in.count = 0;
			if (in.at + 4 > in.size) return false;
			const std::size_t n = in.data[in.at] | (in.data[in.at + 1] << 8);
			in.at += 4;
			if (in.at + n > in.size) return false;
			out.insert(out.end(), in.data + in.at, in.data + in.at + n);
			in.at += n;
			continue;
		}
		if (type == 3) return false;
		Huffman      lit, dist;
		std::uint8_t lengths[320] {};
		if (type == 1) {
			for (int i = 0; i < 144; ++i) lengths[i] = 8;
			for (int i = 144; i < 256; ++i) lengths[i] = 9;
			for (int i = 256; i < 280; ++i) lengths[i] = 7;
			for (int i = 280; i < 288; ++i) lengths[i] = 8;
			lit.build(lengths, 288);
			for (int i = 0; i < 30; ++i) lengths[i] = 5;
			dist.build(lengths, 30);
		} else {
			const int hlit = static_cast<int>(in.get(5)) + 257, hdist = static_cast<int>(in.get(5)) + 1, hclen = static_cast<int>(in.get(4)) + 4;
			std::uint8_t codeLengths[19] {};
			for (int i = 0; i < hclen; ++i) codeLengths[order[i]] = static_cast<std::uint8_t>(in.get(3));
			Huffman code;
			code.build(codeLengths, 19);
			for (int i = 0; i < hlit + hdist;) {
				const int sym = code.decode(in);
				if (sym < 0) return false;
				if (sym < 16) lengths[i++] = static_cast<std::uint8_t>(sym);
				else {
					int repeat = 0, value = 0;
					if (sym == 16) {
						if (!i) return false;
						value = lengths[i - 1], repeat = 3 + static_cast<int>(in.get(2));
					} else if (sym == 17) repeat = 3 + static_cast<int>(in.get(3));
					else repeat = 11 + static_cast<int>(in.get(7));
					if (i + repeat > hlit + hdist) return false;
					while (repeat--) lengths[i++] = static_cast<std::uint8_t>(value);
				}
			}
			lit.build(lengths, hlit);
			dist.build(lengths + hlit, hdist);
		}
		for (;;) {
			const int sym = lit.decode(in);
			if (sym < 0 || in.bad) return false;
			if (sym < 256) out.push_back(static_cast<std::uint8_t>(sym));
			else if (sym == 256) break;
			else {
				const int l = sym - 257;
				if (l >= 29) return false;
				const std::size_t length = lenBase[l] + in.get(lenExtra[l]);
				const int         d      = dist.decode(in);
				if (d < 0 || d >= 30) return false;
				const std::size_t distance = distBase[d] + in.get(distExtra[d]);
				if (distance > out.size()) return false;
				const std::size_t from = out.size() - distance;
				for (std::size_t k = 0; k < length; ++k) out.push_back(out[from + k]);
			}
		}
	}
	return !in.bad;
}

// RGBA pixels of a PNG in memory; false when it is not one this reads.
inline bool Decode(const std::uint8_t* png, std::size_t size, int& width, int& height, std::vector<unsigned char>& rgba) {
	static const std::uint8_t sig[] { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	if (size < 8 || std::memcmp(png, sig, 8)) return false;
	const auto be32 = [](const std::uint8_t* p) { return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]; };
	std::vector<std::uint8_t> idat;
	int                       channels = 0;
	width = height = 0;
	for (std::size_t at = 8; at + 12 <= size;) {
		const std::uint32_t length = be32(png + at);
		const std::uint8_t* type   = png + at + 4;
		const std::uint8_t* body   = png + at + 8;
		if (at + 12 + length > size) return false;
		if (!std::memcmp(type, "IHDR", 4)) {
			width  = static_cast<int>(be32(body));
			height = static_cast<int>(be32(body + 4));
			if (body[8] != 8 || body[12] != 0 || (body[9] != 6 && body[9] != 2)) return false;
			channels = body[9] == 6 ? 4 : 3;
		} else if (!std::memcmp(type, "IDAT", 4)) idat.insert(idat.end(), body, body + length);
		else if (!std::memcmp(type, "IEND", 4)) break;
		at += 12 + length;
	}
	if (width <= 0 || height <= 0 || !channels) return false;
	std::vector<std::uint8_t> raw;
	raw.reserve(static_cast<std::size_t>(width * channels + 1) * height);
	if (!Inflate(idat.data(), idat.size(), raw)) return false;
	const std::size_t stride = static_cast<std::size_t>(width) * channels;
	if (raw.size() < (stride + 1) * height) return false;
	std::vector<std::uint8_t> previous(stride), row(stride);
	rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
	for (int y = 0; y < height; ++y) {
		const std::uint8_t  filter = raw[(stride + 1) * y];
		const std::uint8_t* line   = raw.data() + (stride + 1) * y + 1;
		for (std::size_t x = 0; x < stride; ++x) {
			const int a = x >= static_cast<std::size_t>(channels) ? row[x - channels] : 0, b = previous[x], c = x >= static_cast<std::size_t>(channels) ? previous[x - channels] : 0;
			int       p = line[x];
			switch (filter) {
			case 0: break;
			case 1: p += a; break;
			case 2: p += b; break;
			case 3: p += (a + b) / 2; break;
			case 4: {
				const int pa = std::abs(b - c), pb = std::abs(a - c), pc = std::abs(a + b - 2 * c);
				p += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
				break;
			}
			default: return false;
			}
			row[x] = static_cast<std::uint8_t>(p);
		}
		for (int x = 0; x < width; ++x)
			for (int k = 0; k < channels; ++k) rgba[(static_cast<std::size_t>(y) * width + x) * 4 + k] = row[static_cast<std::size_t>(x) * channels + k];
		previous.swap(row);
	}
	return true;
}

} // namespace PngLite
