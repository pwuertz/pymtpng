#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include <Python.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/string_view.h>

#include "mtpng.hpp"


using namespace std::string_view_literals;
namespace nb = nanobind;
using namespace nb::literals;

using stringv_map = std::map<std::string_view, std::string_view>;

using np_uint8_array_t = nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu>;
using np_uint16_array_t = nb::ndarray<uint16_t, nb::c_contig, nb::device::cpu>;

struct memoryview : public nb::object
{
    memoryview(const uint8_t* mem, const Py_ssize_t size)
        : nb::object(makeMemoryView(mem, size), nb::detail::steal_t{})
    { }

private:
    static PyObject* makeMemoryView(const uint8_t* mem, Py_ssize_t size)
    {
        auto* h = PyMemoryView_FromMemory(const_cast<char*>(reinterpret_cast<const char*>(mem)), size, PyBUF_READ);
        if (!h) {
            throw std::runtime_error("Failed creating memoryview");
        }
        return h;
    }
};

struct Writer
{
    nb::callable py_write;
    std::exception_ptr& eptr;

    size_t write(const uint8_t* bytes, const size_t len) {
        try {
            if (!eptr) {
                return nb::cast<size_t>(py_write(memoryview(bytes, Py_ssize_t(len))));
            }
        } catch(...) {
            eptr = std::current_exception();
        }
        return size_t{0};
    };
};

extern "C" size_t write_func(void* user_data, const uint8_t* bytes, const size_t len)
{
    Writer& writer = *reinterpret_cast<Writer*>(user_data);
    return writer.write(bytes, len);
}

extern "C" bool flush_func([[maybe_unused]] void* user_data)
{
    return true;
}

mtpng_result write_itxt_chunk(mtpng_encoder* p_encoder, const std::string_view& key, const std::string_view& value)
{
    if (key.size() > 79) { throw std::runtime_error("Info key too long"); }

    std::vector<char> chunk;
    // Keyword:             1-79 bytes (character string)
    chunk.insert(chunk.end(), key.begin(), key.end());
    // Null separator:      1 byte
    chunk.push_back(0);
    // Compression flag:    1 byte
    chunk.push_back(0);
    // Compression method:  1 byte
    chunk.push_back(0);
    // Language tag:        0 or more bytes (character string)
    // Null separator:      1 byte
    chunk.push_back(0);
    // Translated keyword:  0 or more bytes
    // Null separator:      1 byte
    chunk.push_back(0);
    // Text:                0 or more bytes
    chunk.insert(chunk.end(), value.begin(), value.end());

    return mtpng_encoder_write_chunk(
        p_encoder, "iTXt", reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
}

mtpng_result write_itxt_chunks(mtpng_encoder* p_encoder, const stringv_map& map)
{
    for (const auto& [key, value] : map) {
        const auto r = write_itxt_chunk(p_encoder, key, value);
        if (r != MTPNG_RESULT_OK) { return r; }
    }
    return MTPNG_RESULT_OK;
}

enum class Dtype {
    U8, U16
};

template <typename T>
void encode_png_impl(
    const nb::ndarray<T, nb::c_contig, nb::device::cpu>& image,
    const Dtype dtype,
    const nb::object& writable,
    const mtpng_filter_t filter,
    const mtpng_strategy_t strategy,
    const mtpng_compression_level_t compression_level,
    const stringv_map& info)
{
    // Get array shape and data
    const auto ndim = image.ndim();
    if (ndim < 2 || ndim > 3) { throw std::runtime_error("Invalid image dimensions"); }
    const auto width = image.shape(1);
    const auto height = image.shape(0);
    const auto nchannels = (ndim > 2) ? image.shape(2) : 1;
    const auto color = [&]() {
        switch (nchannels) {
        case 1: return MTPNG_COLOR_GREYSCALE;
        case 2: return MTPNG_COLOR_GREYSCALE_ALPHA;
        case 3: return MTPNG_COLOR_TRUECOLOR;
        case 4: return MTPNG_COLOR_TRUECOLOR_ALPHA;
        default:
            throw std::runtime_error("Invalid number of channels");
        }
    }();

    // Verifier for mtpng return values (prioritizes exceptions caught in write callback)
    std::exception_ptr eptr;
    const auto TRY = [&](const mtpng_result r) {
        if (eptr) { std::rethrow_exception(eptr); }
        if (r != MTPNG_RESULT_OK) { throw std::runtime_error("mtpng error"); }
    };

    // Create write adapter
    Writer writer {
        .py_write=writable.attr("write"),
        .eptr=eptr,
    };

    // Create threadpool and encoder options
    const auto pool = create_threadpool();
    const auto options = create_encoder_options();
    TRY(mtpng_encoder_options_set_chunk_size(options.get(), 1024*1024));
    TRY(mtpng_encoder_options_set_filter(options.get(), filter));
    TRY(mtpng_encoder_options_set_strategy(options.get(), strategy));
    TRY(mtpng_encoder_options_set_compression_level(options.get(), compression_level));
    TRY(mtpng_encoder_options_set_thread_pool(options.get(), pool.get()));

    // Create PNG header
    const auto header = create_header();
    TRY(mtpng_header_set_size(header.get(), uint32_t(width), uint32_t(height)));
    TRY(mtpng_header_set_color(header.get(), color, (dtype == Dtype::U8) ? 8 : 16));

    // Encode and write PNG
    {
        void* user_data = &writer;
        const auto encoder = create_encoder(write_func, flush_func, user_data, options.get());
        TRY(mtpng_encoder_write_header(encoder.get(), header.get()));
        TRY(write_itxt_chunks(encoder.get(), info));

        const auto nitems_row = width * nchannels;
        if (dtype == Dtype::U8) {
            // Write U8 data rows directly
            const auto* row_data = reinterpret_cast<const uint8_t*>(image.data());
            for (size_t y = 0; y < height; ++y) {
                TRY(mtpng_encoder_write_image_rows(encoder.get(), row_data, 1 * nitems_row));
                row_data += nitems_row;
            }
        } else {
            // Byteswap U16 data before writing rows (TODO: Check native byte order)
            const auto* row_data = reinterpret_cast<const uint16_t*>(image.data());
            std::vector<uint16_t> row_data_be(width * nchannels);
            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    const uint16_t v = row_data[x];
                    row_data_be[x] = ((v & 0x00FF) << 8) | ((v & 0xFF00) >> 8);
                }
                TRY(mtpng_encoder_write_image_rows(
                    encoder.get(), reinterpret_cast<uint8_t*>(row_data_be.data()), 2 * nitems_row));
                row_data += nitems_row;
            }
        }
    }
    // Done
}

void encode_u16_png(
    const np_uint16_array_t& image,
    const nb::object& writable,
    const mtpng_filter_t filter,
    const mtpng_strategy_t strategy,
    const mtpng_compression_level_t compression_level,
    const stringv_map& info)
{
    encode_png_impl(image, Dtype::U16, writable, filter, strategy, compression_level, info);
}

void encode_png(
    const np_uint8_array_t& image,
    const nb::object& writable,
    const mtpng_filter_t filter,
    const mtpng_strategy_t strategy,
    const mtpng_compression_level_t compression_level,
    const stringv_map& info)
{
    encode_png_impl(image, Dtype::U8, writable, filter, strategy, compression_level, info);
}

NB_MODULE(pymtpng, m) {

    nb::enum_<mtpng_compression_level_t>(m, "CompressionLevel")
        .value("Fast", MTPNG_COMPRESSION_LEVEL_FAST)
        .value("Default", MTPNG_COMPRESSION_LEVEL_DEFAULT)
        .value("High", MTPNG_COMPRESSION_LEVEL_HIGH);

    nb::enum_<mtpng_filter_t>(m, "Filter")
        .value("Adaptive", MTPNG_FILTER_ADAPTIVE)
        .value("None_", MTPNG_FILTER_NONE)
        .value("Sub", MTPNG_FILTER_SUB)
        .value("Up", MTPNG_FILTER_UP)
        .value("Average", MTPNG_FILTER_AVERAGE)
        .value("Paeth", MTPNG_FILTER_PAETH);

    nb::enum_<mtpng_strategy_t>(m, "Strategy")
        .value("Adaptive", MTPNG_STRATEGY_ADAPTIVE)
        .value("Default", MTPNG_STRATEGY_DEFAULT)
        .value("Filtered", MTPNG_STRATEGY_FILTERED)
        .value("Huffman", MTPNG_STRATEGY_HUFFMAN)
        .value("Rle", MTPNG_STRATEGY_RLE)
        .value("Fixed", MTPNG_STRATEGY_FIXED);

    m.def(
        "encode_png", &encode_png,
        "image"_a,
        "writable"_a,
        "filter"_a = MTPNG_FILTER_ADAPTIVE,
        "strategy"_a = MTPNG_STRATEGY_RLE,
        "compression_level"_a = MTPNG_COMPRESSION_LEVEL_DEFAULT,
        "info"_a = stringv_map {},
        "Encode PNG to writable object.");

    m.def(
        "encode_u16_png", &encode_u16_png,
        "image"_a,
        "writable"_a,
        "filter"_a = MTPNG_FILTER_ADAPTIVE,
        "strategy"_a = MTPNG_STRATEGY_RLE,
        "compression_level"_a = MTPNG_COMPRESSION_LEVEL_DEFAULT,
        "info"_a = stringv_map {},
        "Encode 16bit PNG to writable object.");


#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)
#ifdef VERSION_INFO    
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
