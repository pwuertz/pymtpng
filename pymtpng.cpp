#include <memory>
#include <vector>
#include <string_view>

#include <Python.h>
#include "pybind11/pybind11.h"
#include "pybind11/numpy.h"

#include "mtpng.hpp"


using namespace std::string_view_literals;
namespace py = pybind11;

constexpr auto VERSION = "0.1"sv;


using np_uint8_array_t = py::array_t<uint8_t, py::array::c_style>;
using np_uint16_array_t = py::array_t<uint16_t, py::array::c_style>;

using write_fn_t = std::function<size_t(const uint8_t* bytes, const size_t len)>;

extern "C" size_t write_func(void* user_data, const uint8_t* bytes, const size_t len)
{
    const write_fn_t& py_write_fn = *reinterpret_cast<const write_fn_t*>(user_data);
    return py_write_fn(bytes, len);
}

extern "C" bool flush_func([[maybe_unused]] void* user_data)
{
    return true;
}

enum class Dtype {
    U8, U16
};

void encode_png_impl(
    const py::array& image,
    const Dtype dtype,
    const py::object& writable,
    const mtpng_filter_t filter,
    const mtpng_strategy_t strategy,
    const mtpng_compression_level_t compression_level)
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

    // Create write callback, store python exceptions
    write_fn_t py_write_fn = [&, writer = writable.attr("write")](const uint8_t* bytes, const size_t len) {
        try {
            if (!eptr) { return writer(py::memoryview::from_memory(bytes, Py_ssize_t(len))).cast<int>(); }
        } catch(...) {
            eptr = std::current_exception();
        }
        return 0;
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
        void* user_data = const_cast<write_fn_t*>(&py_write_fn);
        const auto encoder = create_encoder(write_func, flush_func, user_data, options.get());
        TRY(mtpng_encoder_write_header(encoder.get(), header.get()));

        const auto nitems_row = width * nchannels;
        if (dtype == Dtype::U8) {
            // Write U8 data rows directly
            const auto* row_data = reinterpret_cast<const uint8_t*>(image.data());
            for (int y = 0; y < height; ++y) {
                TRY(mtpng_encoder_write_image_rows(encoder.get(), row_data, 1 * nitems_row));
                row_data += nitems_row;
            }
        } else {
            // Byteswap U16 data before writing rows (TODO: Check native byte order)
            const auto* row_data = reinterpret_cast<const uint16_t*>(image.data());
            std::vector<uint16_t> row_data_be(width * nchannels);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
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
    const py::object& writable,
    const mtpng_filter_t filter,
    const mtpng_strategy_t strategy,
    const mtpng_compression_level_t compression_level)
{
    encode_png_impl(image, Dtype::U16, writable, filter, strategy, compression_level);
}

void encode_png(
    const np_uint8_array_t& image,
    const py::object& writable,
    const mtpng_filter_t filter,
    const mtpng_strategy_t strategy,
    const mtpng_compression_level_t compression_level)
{
    encode_png_impl(image, Dtype::U8, writable, filter, strategy, compression_level);
}

PYBIND11_MODULE(pymtpng, m) {

    m.attr("__version__") = py::str(VERSION.data(), VERSION.size());

    py::enum_<mtpng_compression_level_t>(m, "CompressionLevel")
        .value("Fast", MTPNG_COMPRESSION_LEVEL_FAST)
        .value("Default", MTPNG_COMPRESSION_LEVEL_DEFAULT)
        .value("High", MTPNG_COMPRESSION_LEVEL_HIGH);

    py::enum_<mtpng_filter_t>(m, "Filter")
        .value("Adaptive", MTPNG_FILTER_ADAPTIVE)
        .value("None", MTPNG_FILTER_NONE)
        .value("Sub", MTPNG_FILTER_SUB)
        .value("Up", MTPNG_FILTER_UP)
        .value("Average", MTPNG_FILTER_AVERAGE)
        .value("Paeth", MTPNG_FILTER_PAETH);

    py::enum_<mtpng_strategy_t>(m, "Strategy")
        .value("Adaptive", MTPNG_STRATEGY_ADAPTIVE)
        .value("Default", MTPNG_STRATEGY_DEFAULT)
        .value("Filtered", MTPNG_STRATEGY_FILTERED)
        .value("Huffman", MTPNG_STRATEGY_HUFFMAN)
        .value("Rle", MTPNG_STRATEGY_RLE)
        .value("Fixed", MTPNG_STRATEGY_FIXED);

    m.def(
        "encode_png", &encode_png, "Encode PNG to writable object.",
        py::arg("image"), py::arg("writable"),
        py::arg("filter") = MTPNG_FILTER_ADAPTIVE,
        py::arg("strategy") = MTPNG_STRATEGY_RLE,
        py::arg("compression_level") = MTPNG_COMPRESSION_LEVEL_DEFAULT);

    m.def(
        "encode_u16_png", &encode_u16_png, "Encode 16bit PNG to writable object.",
        py::arg("image"), py::arg("writable"),
        py::arg("filter") = MTPNG_FILTER_ADAPTIVE,
        py::arg("strategy") = MTPNG_STRATEGY_RLE,
        py::arg("compression_level") = MTPNG_COMPRESSION_LEVEL_DEFAULT);
}
