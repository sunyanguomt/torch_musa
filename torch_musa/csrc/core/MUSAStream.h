#ifndef TORCH_MUSA_CSRC_CORE_MUSA_STREAM_H_
#define TORCH_MUSA_CSRC_CORE_MUSA_STREAM_H_

#include <c10/core/DeviceGuard.h>
#include <c10/core/Stream.h>
#include <c10/util/CallOnce.h>
#include <c10/util/irange.h>

#include "musa_runtime_api.h"
#include "torch_musa/csrc/aten/utils/Utils.h"
#include "torch_musa/csrc/core/Device.h"
#include "torch_musa/csrc/core/MUSAException.h"

namespace c10 {
namespace musa {

using at::musa::kMUSA;
using c10::Device;
using c10::DeviceIndex;
using c10::DeviceType;
using c10::Stream;
using c10::StreamId;

class MUSAStream {
 public:
  enum Unchecked { UNCHECKED };

  explicit MUSAStream(Stream stream) : stream_(stream) {
    TORCH_CHECK(stream_.device_type() == kMUSA);
  }

  explicit MUSAStream(Unchecked, Stream stream) : stream_(stream) {}

  bool operator==(const MUSAStream& other) const noexcept {
    return unwrap() == other.unwrap();
  }

  bool operator!=(const MUSAStream& other) const noexcept {
    return unwrap() != other.unwrap();
  }

  operator musaStream_t() const {
    return stream();
  }

  operator Stream() const {
    return unwrap();
  }

  DeviceType device_type() const {
    return kMUSA;
  }

  // Get the MUSA device index that this stream is associated with.
  DeviceIndex device_index() const {
    return stream_.device_index();
  }

  // Get the full Device that this stream is associated wtih.
  Device device() const {
    return Device(kMUSA, device_index());
  }

  StreamId id() const {
    return stream_.id();
  }

  bool query() const {
    c10::DeviceGuard guard(stream_.device());
    const auto err = TORCH_MUSA_ERROR_HANDLE(musaStreamQuery(stream()));
    if (err == musaSuccess) {
      return true;
    } else if (err != musaErrorNotReady) {
      TORCH_MUSA_CHECK(err);
    } else {
      // ignore and clear the error if not ready
      (void)musaGetLastError();
    }

    return false;
  }

  void synchronize() const {
    c10::DeviceGuard guard{stream_.device()};
    TORCH_MUSA_CHECK(musaStreamSynchronize(stream()));
  }

  int priority() const {
    c10::DeviceGuard guard{stream_.device()};
    int priority = 0;
    TORCH_MUSA_CHECK(musaStreamGetPriority(stream(), &priority));
    return priority - 1; // musa priority level [1, 0], cuda level [0, -1]
  }

  musaStream_t stream() const;

  Stream unwrap() const {
    return stream_;
  }

  struct c10::StreamData3 pack3() const {
    return stream_.pack3();
  }

  // Unpack a MUSAStream from the 3 fields generated by pack().
  static MUSAStream unpack3(
      StreamId stream_id,
      DeviceIndex device_index,
      DeviceType device_type) {
    return MUSAStream(Stream::unpack3(stream_id, device_index, device_type));
  }

  static std::tuple<int, int> priority_range() {
    int least_priority, greatest_priority;
    TORCH_MUSA_CHECK(
        musaDeviceGetStreamPriorityRange(&least_priority, &greatest_priority));
    TORCH_INTERNAL_ASSERT(
        least_priority >= 1, "Unexpected MUSA stream priority range");
    TORCH_INTERNAL_ASSERT(
        greatest_priority <= 0, "Unexpected MUSA stream priority range");
    return std::make_tuple(0, -1);
  }

 private:
  Stream stream_;
};

/**
 * Get a new stream from the MUSA stream pool.  You can think of this
 * as "creating" a new stream, but no such creation actually happens;
 * instead, streams are preallocated from the pool and returned in a
 * round-robin fashion.
 *
 * You can request a stream from the high priority pool by setting
 * isHighPriority to true, or a stream for a specific device by setting device
 * (defaulting to the current MUSA stream.)
 */
MUSAStream getStreamFromPool(
    const bool isHighPriority = false,
    DeviceIndex device = -1);

/**
 * Get a MUSAStream from a externally allocated one.
 *
 * This is mainly for interoperability with different libraries where we
 * want to operate on a non-torch allocated stream for data exchange or similar
 * purposes
 */
MUSAStream getStreamFromExternal(
    musaStream_t ext_stream,
    DeviceIndex device_index);

/**
 * Get the default MUSA stream, for the passed MUSA device, or for the
 * current device if no device index is passed.  The default stream is
 * where most computation occurs when you aren't explicitly using
 * streams.
 */
MUSAStream getDefaultMUSAStream(DeviceIndex device_index = -1);

/**
 * Get the current MUSA stream, for the passed MUSA device, or for the
 * current device if no device index is passed.  The current MUSA stream
 * will usually be the default MUSA stream for the device, but it may
 * be different if someone called 'setCurrentMUSAStream' or used 'StreamGuard'
 * or 'MUSAStreamGuard'.
 */
MUSAStream getCurrentMUSAStream(DeviceIndex device_index = -1);

/**
 * Set the current stream on the device of the passed in stream to be
 * the passed in stream.  Yes, you read that right: this function
 * has *nothing* to do with the current device: it toggles the current
 * stream of the device of the passed stream.
 *
 * Confused?  Avoid using this function; prefer using 'MUSAStreamGuard' instead
 * (which will switch both your current device and current stream in the way you
 * expect, and reset it back to its original state afterwards).
 */
void setCurrentMUSAStream(MUSAStream stream);

std::ostream& operator<<(std::ostream& stream, const MUSAStream& s);

} // namespace musa
} // namespace c10

namespace std {
template <>
struct hash<c10::musa::MUSAStream> {
  size_t operator()(c10::musa::MUSAStream s) const noexcept {
    return std::hash<c10::Stream>{}(s.unwrap());
  }
};

} // namespace std

#endif // TORCH_MUSA_CSRC_CORE_MUSA_STREAM_H_