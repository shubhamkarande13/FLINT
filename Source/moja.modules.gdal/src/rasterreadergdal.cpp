#include "moja/modules/gdal/rasterreadergdal.h"

#include "moja/logging.h"

#include "gdalcpp.h"

#include <moja/flint/flintexceptions.h>

#include <moja/datarepository/tileblockcellindexer.h>

#include <fmt/format.h>

namespace moja {
namespace modules {
namespace gdal {

std::tuple<double, double> seq2geog(const int tile_idx) {
   const double lon = tile_idx % 360 - 180;
   const double lat = 90 - tile_idx / 360;
   return std::make_tuple(lon, lat);
}

std::tuple<double, double> seqChunk2geog(int tile_idx, int block_idx) {
   const double lon = tile_idx % 360 - 180;
   const double lat = 90 - tile_idx / 360;

   const double x = block_idx % 10;
   const double y = block_idx / 10;

   return std::make_tuple(lon + x / 10, lat - y / 10);
}

MetaDataRasterReaderGDAL::MetaDataRasterReaderGDAL(const std::string& path, const std::string& prefix,
                                                   const DynamicObject& settings)
    : MetaDataRasterReaderInterface(path, prefix, settings) {
   try {
      // TODO: Fix this
      _prefix = prefix;
      _path = prefix;
   } catch (...) {
      BOOST_THROW_EXCEPTION(flint::LocalDomainError()
                            << flint::Details("GDAL Error in constructor") << flint::LibraryName("moja.modules.gdal")
                            << flint::ModuleName(BOOST_CURRENT_FUNCTION) << flint::ErrorCode(1));
   }
}

DynamicObject MetaDataRasterReaderGDAL::readMetaData() const { return {}; }

TileRasterReaderGDAL::TileRasterReaderGDAL(const std::string& path, const Point& origin, const std::string& prefix,
                                           const datarepository::TileBlockCellIndexer& indexer,
                                           const DynamicObject& settings)
    : TileRasterReaderInterface(path, origin, prefix, indexer, settings), _path(path), _prefix(prefix) {
   _variable = settings["name"].extract<const std::string>();

   _runId = "NA";
   if (settings.contains("system_runid")) {
      _runId = settings["system_runid"].extract<const std::string>();
   }
}

TileRasterReaderGDAL::~TileRasterReaderGDAL() = default;

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<Int8>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(Int8));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<UInt8>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(UInt8));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<Int16>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(Int16));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<UInt16>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(UInt16));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<Int32>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(Int32));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<UInt32>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(UInt32));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<Int64>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(Int64));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<UInt64>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(UInt64));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<float>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(float));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, std::vector<double>* block) const {
   readBlockData(blkIdx, reinterpret_cast<char*>(block->data()), block->size() * sizeof(double));
}

void TileRasterReaderGDAL::readBlockData(const datarepository::BlockIdx& blkIdx, char* block_data,
                                         size_t block_size) const {
   const auto tile_idx = blkIdx.tileIdx;
   const auto block_idx = blkIdx.blockIdx;

   auto start_time = std::chrono::system_clock::now();

   try {
      gdalcpp::dataset ds(_path);

      auto& gdal_ds = ds.get();
      double geo_transform[6];
      auto err = gdal_ds.GetGeoTransform(geo_transform);
      if (err != CE_None) {
         throw gdalcpp::gdal_error{std::string{"cant get Geo Transform for: '"} + _path, err};
      }
      double lat, lon;

      std::tie(lon, lat) = seqChunk2geog(tile_idx, block_idx);
      const auto ulx = lon;
      const auto uly = lat;
      const auto lrx = lon + 0.1;
      const auto lry = lat - 0.1;

      double src_win[4];
      src_win[0] = std::floor((ulx - geo_transform[0]) / geo_transform[1] + 0.001);
      src_win[1] = std::floor((uly - geo_transform[3]) / geo_transform[5] + 0.001);

      src_win[2] = std::floor((lrx - ulx) / geo_transform[1] + 0.5);
      src_win[3] = std::floor((lry - uly) / geo_transform[5] + 0.5);

      std::vector<uint8_t> buff;
      auto band = ds.get().GetRasterBand(1);

      buff.resize(400 * 400, 255);
      err = band->RasterIO(GF_Read, int(src_win[0]), int(src_win[1]), int(src_win[2]), int(src_win[3]), &buff[0], 400,
                           400, GDT_Byte, 0, 0);
      if (err == CE_None) {
         if (block_size <= buff.size()) {
            std::uninitialized_copy(buff.begin(), buff.end(), reinterpret_cast<char*>(block_data));

            auto end_time = std::chrono::system_clock::now();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.0;
            MOJA_LOG_INFO << "RunId (" << _runId << ") - "
                          << "GDAL Read - variable (" << _variable << "), target (" << _path << "), time (" << elapsed
                          << ")";
         } else {
            MOJA_LOG_ERROR << "RunId (" << _runId << ") - "
                           << "GDAL - read error buffer too small, target (" << _path << ")";
            const auto str = fmt::format("GDAL read error buffer too small: {}", _path);
            BOOST_THROW_EXCEPTION(flint::LocalDomainError()
                                  << flint::Details(str) << flint::LibraryName("moja.modules.mulliongroup.cog")
                                  << flint::ModuleName(BOOST_CURRENT_FUNCTION) << flint::ErrorCode(1));
         }
      }
   } catch (const gdalcpp::gdal_error& err) {
      MOJA_LOG_ERROR << "RunId (" << _runId << ") - "
                     << "GDAL - gdal error for target (" << _path << ") - " << err.what();
   } catch (...) {
      MOJA_LOG_ERROR << "RunId (" << _runId << ") - "
                     << "GDAL - exception (" << _path << ")";
      throw;
   }
}
}  // namespace gdal

}  // namespace modules
}  // namespace moja