#include <ATen/Config.h>
#include <ATen/NamedTensorUtils.h>
#include <ATen/WrapDimUtilsMulti.h>
#include <ATen/native/ReduceOpsUtils.h>
#include <ATen/ops/max.h>
#include <torch/library.h>
#include <sstream>

#ifndef AT_PER_OPERATOR_HEADERS
#include <ATen/Functions.h>
#include <ATen/NativeFunctions.h>
#else
#include <ATen/ops/_cummax_helper.h>
#include <ATen/ops/_cummax_helper_native.h>
#include <ATen/ops/_cummin_helper.h>
#include <ATen/ops/_cummin_helper_native.h>
#include <ATen/ops/_logcumsumexp.h>
#include <ATen/ops/_logcumsumexp_native.h>
#include <ATen/ops/_sparse_sum.h>
#include <ATen/ops/_sparse_sum_native.h>
#include <ATen/ops/add.h>
#include <ATen/ops/all_meta.h>
#include <ATen/ops/all_native.h>
#include <ATen/ops/amax.h>
#include <ATen/ops/amax_meta.h>
#include <ATen/ops/amax_native.h>
#include <ATen/ops/amin_meta.h>
#include <ATen/ops/amin_native.h>
#include <ATen/ops/aminmax_meta.h>
#include <ATen/ops/aminmax_native.h>
#include <ATen/ops/any_meta.h>
#include <ATen/ops/any_native.h>
#include <ATen/ops/argmax_meta.h>
#include <ATen/ops/argmax_native.h>
#include <ATen/ops/argmin_meta.h>
#include <ATen/ops/argmin_native.h>
#include <ATen/ops/cat.h>
#include <ATen/ops/complex.h>
#include <ATen/ops/cummax.h>
#include <ATen/ops/cummax_native.h>
#include <ATen/ops/cummaxmin_backward_native.h>
#include <ATen/ops/cummin.h>
#include <ATen/ops/cummin_native.h>
#include <ATen/ops/cumprod.h>
#include <ATen/ops/cumprod_backward_native.h>
#include <ATen/ops/cumprod_meta.h>
#include <ATen/ops/cumprod_native.h>
#include <ATen/ops/cumsum.h>
#include <ATen/ops/cumsum_meta.h>
#include <ATen/ops/cumsum_native.h>
#include <ATen/ops/diff_native.h>
#include <ATen/ops/dist_native.h>
#include <ATen/ops/empty.h>
#include <ATen/ops/empty_like.h>
#include <ATen/ops/equal_native.h>
#include <ATen/ops/exp.h>
#include <ATen/ops/gather.h>
#include <ATen/ops/gradient_native.h>
#include <ATen/ops/imag.h>
#include <ATen/ops/isnan_native.h>
#include <ATen/ops/linalg_vector_norm.h>
#include <ATen/ops/logcumsumexp.h>
#include <ATen/ops/logcumsumexp_native.h>
#include <ATen/ops/logical_xor.h>
#include <ATen/ops/logsumexp.h>
#include <ATen/ops/logsumexp_native.h>
#include <ATen/ops/mean.h>
#include <ATen/ops/mean_meta.h>
#include <ATen/ops/mean_native.h>
#include <ATen/ops/min.h>
#include <ATen/ops/nanmean_native.h>
#include <ATen/ops/nansum.h>
#include <ATen/ops/nansum_native.h>
#include <ATen/ops/narrow.h>
#include <ATen/ops/native_norm.h>
#include <ATen/ops/norm.h>
#include <ATen/ops/norm_meta.h>
#include <ATen/ops/norm_native.h>
#include <ATen/ops/ones.h>
#include <ATen/ops/prod.h>
#include <ATen/ops/prod_meta.h>
#include <ATen/ops/prod_native.h>
#include <ATen/ops/real.h>
#include <ATen/ops/slice.h>
#include <ATen/ops/special_logsumexp_native.h>
#include <ATen/ops/sqrt.h>
#include <ATen/ops/squeeze.h>
#include <ATen/ops/stack.h>
#include <ATen/ops/std.h>
#include <ATen/ops/std_mean.h>
#include <ATen/ops/std_mean_native.h>
#include <ATen/ops/std_native.h>
#include <ATen/ops/sub.h>
#include <ATen/ops/sum.h>
#include <ATen/ops/sum_meta.h>
#include <ATen/ops/sum_native.h>
#include <ATen/ops/trace_native.h>
#include <ATen/ops/value_selecting_reduction_backward_native.h>
#include <ATen/ops/var.h>
#include <ATen/ops/var_mean.h>
#include <ATen/ops/var_mean_native.h>
#include <ATen/ops/var_native.h>
#include <ATen/ops/zeros.h>
#include <ATen/ops/zeros_like.h>
#endif

#include "torch_musa/csrc/aten/ops/TensorFactory.h"
#include "torch_musa/csrc/aten/utils/Utils.h"
#include "torch_musa/csrc/utils/musa_lazy_init.h"

#include <mudnn.h>

namespace at {
namespace musa {

// Copy from ReduceOps.cpp
inline ScalarType musa_get_dtype_from_self(
    const Tensor& self,
    const optional<ScalarType>& dtype,
    bool promote_integers) {
  if (dtype.has_value()) {
    return dtype.value();
  }
  ScalarType src_type = self.scalar_type();
  if (promote_integers && at::isIntegralType(src_type, /*includeBool=*/true)) {
    return kLong;
  }
  return src_type;
}

// Copy from ReduceOps.cpp
ScalarType musa_infer_dtype_from_optional(
    const Tensor& self,
    const optional<ScalarType>& opt_dtype,
    const Tensor& result) {
  // 'opt_dtype' has the priority for both cases.
  if (result.defined()) {
    // Otherwise, get the result type, if defined.
    return opt_dtype.value_or(result.scalar_type());
  } else {
    // Last case is to get the self type.
    // If the self type is an integer, we promote it to kLong.
    return musa_get_dtype_from_self(self, opt_dtype, true);
  }
}

void ReduceCall(
    Tensor& output,
    const Tensor& self,
    IntArrayRef dim,
    ::musa::dnn::Reduce::Mode m,
    const c10::optional<at::Scalar>& p = c10::nullopt,
    const bool is_norm = false) {
  c10::musa::MUSAGuard device_guard(self.device());
  if (C10_UNLIKELY(self.numel() == 0)) {
    return;
  }
  auto input = Contiguous(self);
  auto out = CreateMUTensor(output);
  auto in = CreateMUTensor(input);

  muHandle& h = GetMudnnHandle();
  ::musa::dnn::Reduce r;
  CHECK_MUDNN_STATUS(r.SetMode(m), "SetMode");
  // That input is scalar, but dim = [0] is allowed in PyTorch, in which case
  // we need pass an empty 'dim' paramter to Reduce in muDNN.
  if (self.dim() == 0 && self.numel() == 1) {
    CHECK_MUDNN_STATUS(r.SetDim({}), "SetDim");
  } else {
    std::vector<int> dim_int(dim.begin(), dim.end());
    CHECK_MUDNN_STATUS(r.SetDim(dim_int.size(), dim_int.data()), "SetDim");
  }
  // set order parameter for norm op
  if (is_norm) {
    float p_value = 2.0f;
    if (p.has_value()) {
      auto val = p.value();
      if (val.isIntegral(false)) {
        p_value = static_cast<float>(val.to<int64_t>());
      } else if (val.isFloatingPoint()) {
        p_value = static_cast<float>(val.to<double>());
      } else {
        TORCH_CHECK(
            false, "norm_kernel_musa_impl expects norm to be integer or float");
      }
    }
    CHECK_MUDNN_STATUS(r.SetNormOrd(p_value), "SetNormOrd");
  }

  CHECK_MUDNN_STATUS(r.Run(h, out, in, InternalMemAlloc), "Run");
}

Tensor Reduction(
    const Tensor& self,
    IntArrayRef dim,
    bool keepdim,
    optional<ScalarType> out_dtype,
    ::musa::dnn::Reduce::Mode m,
    const c10::optional<at::Scalar>& p = c10::nullopt,
    const bool is_norm = false) {
  c10::musa::MUSAGuard device_guard(self.device());
  out_dtype = musa_infer_dtype_from_optional(self, out_dtype, Tensor());
  DimVector dims_vec(dim);
  maybe_wrap_dims(dims_vec, self.dim());
  auto shape = at::meta::get_reduction_shape(self, dims_vec, keepdim);

  Tensor output = at::empty(shape, self.options().dtype(out_dtype));
  namedinference::propagate_names_for_reduction(
      output, self, dims_vec, keepdim);

  if (self.numel() == 0) {
    output.zero_();
  } else {
    ReduceCall(output, self, dims_vec, m, p, is_norm);
  }
  return output;
}

#define REDUCE_OPERATOR(op_name)
Tensor Mean(const Tensor& self, c10::optional<ScalarType> dtype) {
  return Reduction(
      self, IntArrayRef{}, false, dtype, ::musa::dnn::Reduce::Mode::MEAN);
}

Tensor MeanDim(
    const Tensor& self,
    at::OptionalIntArrayRef dim,
    bool keepdim,
    c10::optional<ScalarType> dtype) {
  return Reduction(
      self, dim.value(), keepdim, dtype, ::musa::dnn::Reduce::Mode::MEAN);
}

Tensor& MeanOut(
    const Tensor& self,
    at::OptionalIntArrayRef dim,
    bool keepdim,
    c10::optional<ScalarType> dtype,
    Tensor& output) {
  ReduceCall(output, self, dim.value(), ::musa::dnn::Reduce::Mode::MEAN);
  return output;
}

Tensor MeanNamesDim(
    const Tensor& self,
    DimnameList dim,
    bool keepdim,
    c10::optional<ScalarType> dtype) {
  return Reduction(
      self,
      dimnames_to_positions(self, dim),
      keepdim,
      dtype,
      ::musa::dnn::Reduce::Mode::MEAN);
}

Tensor& MeanNamesDimOut(
    const Tensor& self,
    DimnameList dim,
    bool keepdim,
    c10::optional<ScalarType> dtype,
    Tensor& output) {
  ReduceCall(
      output,
      self,
      dimnames_to_positions(self, dim),
      ::musa::dnn::Reduce::Mode::MEAN);
  return output;
}

Tensor Sum(const Tensor& self, c10::optional<ScalarType> dtype) {
  return Reduction(
      self, IntArrayRef{}, false, dtype, ::musa::dnn::Reduce::Mode::ADD);
}

Tensor& SumIntListOut(
    const Tensor& self,
    at::OptionalIntArrayRef dim,
    bool keepdim,
    optional<ScalarType> opt_dtype,
    Tensor& output) {
  c10::musa::MUSAGuard device_guard(self.device());
  DimVector dims_vec(dim.value());
  maybe_wrap_dims(dims_vec, self.dim());
  auto shape = at::meta::get_reduction_shape(self, dims_vec, keepdim);
  output.resize_(shape);
  ReduceCall(output, self, dim.value(), ::musa::dnn::Reduce::Mode::ADD);
  return output;
}

Tensor SumDimnameList(
    const Tensor& self,
    DimnameList dim,
    bool keepdim,
    c10::optional<ScalarType> dtype) {
  return Reduction(
      self,
      dimnames_to_positions(self, dim),
      keepdim,
      dtype,
      ::musa::dnn::Reduce::Mode::ADD);
}

Tensor& SumDimnameListOut(
    const Tensor& self,
    DimnameList dim,
    bool keepdim,
    c10::optional<ScalarType> dtype,
    Tensor& output) {
  UNUSED(dtype);
  UNUSED(keepdim);
  ReduceCall(
      output,
      self,
      dimnames_to_positions(self, dim),
      ::musa::dnn::Reduce::Mode::ADD);
  return output;
}

Tensor SumIntList(
    const Tensor& self,
    at::OptionalIntArrayRef dim,
    bool keepdim,
    optional<ScalarType> opt_dtype) {
  return Reduction(
      self, dim.value(), keepdim, opt_dtype, ::musa::dnn::Reduce::Mode::ADD);
}

Tensor Prod(const Tensor& self, c10::optional<ScalarType> dtype) {
  return Reduction(
      self, IntArrayRef{}, false, dtype, ::musa::dnn::Reduce::Mode::PROD);
}

Tensor& ProdIntOut(
    const Tensor& self,
    long dim,
    bool keepdim,
    c10::optional<ScalarType> dtype,
    Tensor& output) {
  ReduceCall(output, self, dim, ::musa::dnn::Reduce::Mode::PROD);
  return output;
}

Tensor& NormDtypeOut(
    const Tensor& self,
    const c10::optional<at::Scalar>& p,
    at::IntArrayRef dim,
    bool keepdim,
    at::ScalarType dtype,
    at::Tensor& out) {
  TORCH_CHECK(
      self.scalar_type() == at::ScalarType::Float ||
          self.scalar_type() == at::ScalarType::Half ||
          self.scalar_type() == at::ScalarType::BFloat16,
      "Dtype of input tensor of Norm.out only support Float32, Half, BFloat16. ",
      "but now it is ",
      self.scalar_type());
  TORCH_CHECK(
      dtype == at::ScalarType::Float || dtype == at::ScalarType::Half ||
          dtype == at::ScalarType::BFloat16,
      "Dtype of input tensor of Norm.out only support Float32, Half, BFloat16. ",
      "but now it is ",
      self.scalar_type());
  auto out_dtype = out.scalar_type();

  // special case for type promotion in mixed precision, improves computational
  // efficiency.
  const bool gpu_lowp_to_f32 =
      (self.scalar_type() == kHalf || self.scalar_type() == kBFloat16) &&
      out_dtype == kFloat;

  Tensor out_temp = Reduction(
      self,
      dim,
      keepdim,
      gpu_lowp_to_f32 ? self.scalar_type() : out_dtype,
      ::musa::dnn::Reduce::Mode::NORM,
      p,
      true);
  out.copy_(out_temp);
  return out;
}

Tensor& NormOut(
    const Tensor& self,
    const c10::optional<at::Scalar>& p,
    at::IntArrayRef dim,
    bool keepdim,
    Tensor& out) {
  out = NormDtypeOut(self, p, dim, keepdim, self.scalar_type(), out);
  return out;
}

namespace {

using CUM_MODE = ::musa::dnn::Cum::Mode;

void CumulativeImpl(Tensor& out, const Tensor& in, int64_t dim, CUM_MODE mode) {
  namedinference::propagate_names(out, in);

  if (in.numel() == 0) {
    return;
  }
  if (in.dim() == 0) {
    out.fill_(in);
  }

  const c10::musa::MUSAGuard device_guard(in.device());
  auto in_ctype = in.scalar_type();
  auto out_ctype = out.scalar_type();

  if (isFloatingType(in_ctype) || isFloatingType(out_ctype)) {
    out_ctype = promoteTypes(in_ctype, out_ctype);
    in_ctype = out_ctype;
  } else if (out_ctype == ScalarType::Int || out_ctype == ScalarType::Long) {
    if (in_ctype == ScalarType::Byte || in_ctype == ScalarType::Short) {
      in_ctype = ScalarType::Int;
    } else if (in_ctype == ScalarType::Long) {
      out_ctype = in_ctype;
    }
  } else {
    // Special cases for muDNN workarounds.
    if (in_ctype == ScalarType::Long) {
      out_ctype = in_ctype;
    } else {
      out_ctype = ScalarType::Int;
      if (in_ctype == ScalarType::Byte || in_ctype == ScalarType::Short) {
        in_ctype = out_ctype;
      }
    }
  }

  using Proxy = typename c10::MaybeOwned<Tensor>;
  auto make_proxy = [](const Tensor& in, ScalarType dtype) -> Proxy {
    if (dtype != in.scalar_type()) {
      return Proxy::owned(in.to(dtype));
    }
    return Proxy::borrowed(in);
  };
  auto c_in = make_proxy(in, in_ctype);
  auto c_out = make_proxy(out, out_ctype);
  auto contig_c_in = FormatContiguous(*c_in, MemoryFormat::Contiguous);
  auto contig_c_out = FormatContiguous(*c_out, MemoryFormat::Contiguous);

  auto mudnn_out = CreateMUTensor(contig_c_out);
  auto mudnn_in = CreateMUTensor(contig_c_in);
  muHandle& h = GetMudnnHandle();
  ::musa::dnn::Cum cum;
  CHECK_MUDNN_STATUS(cum.SetDim(static_cast<int>(dim)), "SetDim");
  CHECK_MUDNN_STATUS(cum.SetMode(mode), "SetMode");
  CHECK_MUDNN_STATUS(
      cum.Run(h, mudnn_out, mudnn_in, InternalMemAlloc), "CumRun");

  if (out_ctype != out.scalar_type()) {
    out.copy_(contig_c_out);
  }
}

Tensor& CumulativeOut(
    const Tensor& self,
    int64_t dim,
    c10::optional<ScalarType> dtype,
    Tensor& out,
    CUM_MODE mode) {
  // Keep logical consistency with torch.
  if (dtype) {
    const auto out_dtype = out.scalar_type();
    const auto expect_dtype = dtype.value();
    TORCH_CHECK(
        expect_dtype == out_dtype,
        "Expected out tensor to have dtype ",
        expect_dtype,
        ", but got ",
        out_dtype,
        " instead");
  }
  native::resize_output(out, self.sizes());
  CumulativeImpl(out, self, dim, mode);
  return out;
}

Tensor& Cumulative_(
    Tensor& self,
    int64_t dim,
    c10::optional<ScalarType> dtype,
    CUM_MODE mode) {
  // Since check_inplace(...) does not modify the `self` tensor in torch,
  // we ignore the `dtype` parameter here, even though it may have a value.
  CumulativeImpl(self, self, dim, mode);
  return self;
}

Tensor Cumulative(
    const Tensor& self,
    int64_t dim,
    c10::optional<ScalarType> dtype,
    CUM_MODE mode) {
  const auto in_dtype = self.scalar_type();
  const auto out_dtype = dtype.value_or(
      isIntegralType(in_dtype, true) ? ScalarType::Long : in_dtype);
  Tensor out = at::empty(self.sizes(), self.options().dtype(out_dtype));
  CumulativeImpl(out, self, dim, mode);
  return out;
}

} // anonymous namespace

#define GEN_CUMULATIVE_FUNCTION(OP, MODE)                                     \
  Tensor OP(                                                                  \
      const Tensor& self, int64_t dim, c10::optional<ScalarType> dtype) {     \
    return Cumulative(self, dim, dtype, MODE);                                \
  }                                                                           \
                                                                              \
  Tensor& OP##_(Tensor& self, int64_t dim, c10::optional<ScalarType> dtype) { \
    return Cumulative_(self, dim, dtype, MODE);                               \
  }                                                                           \
                                                                              \
  Tensor& OP##Out(                                                            \
      const Tensor& self,                                                     \
      int64_t dim,                                                            \
      c10::optional<ScalarType> dtype,                                        \
      Tensor& out) {                                                          \
    return CumulativeOut(self, dim, dtype, out, MODE);                        \
  }

GEN_CUMULATIVE_FUNCTION(CumSum, CUM_MODE::ADD)
GEN_CUMULATIVE_FUNCTION(CumProd, CUM_MODE::MUL)

#undef GEN_CUMULATIVE_FUNCTION

Tensor Any(const Tensor& self) {
  Tensor out = Reduction(
      self,
      IntArrayRef{},
      false,
      self.scalar_type(),
      ::musa::dnn::Reduce::Mode::OR);
  if (self.scalar_type() != ScalarType::Bool) {
    out = out.to(ScalarType::Bool);
  }
  return out;
}

Tensor& AnyOut(const Tensor& self, Tensor& out) {
  IntArrayRef dims = {};
  ReduceCall(out, self, dims, ::musa::dnn::Reduce::Mode::OR);
  if (self.scalar_type() != ScalarType::Bool) {
    out = out.to(ScalarType::Bool);
  }
  return out;
}

std::string concatenate(
    const std::string& str,
    ScalarType scalaType,
    const Tensor& self) {
  std::ostringstream oss;
  oss << str << scalaType << ": " << self;
  return oss.str();
}

Tensor AnyDim(const Tensor& self, int64_t dim, bool keepdim) {
  TORCH_CHECK(
      self.scalar_type() == ScalarType::Bool || self.item<int>() == 0 ||
          self.item<int>() == 1,
      concatenate(
          "Now only support bool type or 0/1 value, but got ",
          self.scalar_type(),
          self));
  IntArrayRef dims(dim);
  return Reduction(
      self, {dim}, keepdim, self.scalar_type(), ::musa::dnn::Reduce::Mode::OR);
}

Tensor& AnyDimOut(const Tensor& self, int64_t dim, bool keepdim, Tensor& out) {
  UNUSED(keepdim);
  TORCH_CHECK(
      self.scalar_type() == ScalarType::Bool || self.item<int>() == 0 ||
          self.item<int>() == 1,
      concatenate(
          "Now only support bool type or 0/1 value, but got ",
          self.scalar_type(),
          self));
  IntArrayRef dims(dim);
  ReduceCall(out, self, dims, ::musa::dnn::Reduce::Mode::OR);
  return out;
}

void ReduceIndicesCall(
    Tensor& output,
    Tensor& indices,
    const Tensor& self,
    int64_t dim,
    ::musa::dnn::Reduce::Mode m) {
  TORCH_CHECK(
      self.scalar_type() == output.scalar_type(),
      "scalar_type of in&out must be the same, bug got: ",
      self.scalar_type(),
      " and: ",
      output.scalar_type());

  c10::musa::MUSAGuard device(self.device());
  Tensor out_tmp = FormatContiguous(output, at::MemoryFormat::Contiguous);
  Tensor indices_tmp = FormatContiguous(indices, at::MemoryFormat::Contiguous);

  auto out = CreateMUTensor(out_tmp);
  auto ids = CreateMUTensor(indices_tmp);
  auto in = CreateMUTensor(self, /*permute_if_not_contiguous=*/false);

  muHandle& h = GetMudnnHandle();
  ::musa::dnn::Reduce r;
  CHECK_MUDNN_STATUS(r.SetMode(m), "SetMode");
  int dim_int = dim;
  CHECK_MUDNN_STATUS(r.SetDim({dim_int}), "SetDim");
  CHECK_MUDNN_STATUS(
      r.RunWithIndices(h, out, ids, in, InternalMemAlloc), "RunWithIndices");

  if (C10_UNLIKELY(!output.is_same(out_tmp))) {
    output.copy_(out_tmp);
  }
  if (C10_UNLIKELY(!indices.is_same(indices_tmp))) {
    indices.copy_(indices_tmp);
  }
}

void ReduceIndicesOnlyCall(
    Tensor& output,
    const Tensor& self,
    int64_t dim,
    ::musa::dnn::Reduce::Mode m) {
  c10::musa::MUSAGuard device(self.device());

  Tensor out_tmp = FormatContiguous(output, at::MemoryFormat::Contiguous);
  auto out = CreateMUTensor(out_tmp);
  auto in = CreateMUTensor(self, /*permute_if_not_contiguous=*/false);

  muHandle& h = GetMudnnHandle();
  ::musa::dnn::Reduce r;
  CHECK_MUDNN_STATUS(r.SetMode(m), "SetMode");
  int dim_int = dim;
  CHECK_MUDNN_STATUS(r.SetDim({dim_int}), "SetDim");
  CHECK_MUDNN_STATUS(r.RunIndices(h, out, in, InternalMemAlloc), "RunIndices");
  if (C10_UNLIKELY(!output.is_same(out_tmp))) {
    output.copy_(out_tmp);
  }
}

std::tuple<Tensor, Tensor> ReductionIndices(
    const Tensor& self,
    int64_t dim,
    bool keepdim,
    ::musa::dnn::Reduce::Mode m) {
  dim = maybe_wrap_dim(dim, self.dim());

  IntArrayRef dims(dim);
  DimVector dims_vec(dims);
  maybe_wrap_dims(dims_vec, self.dim());
  auto shape = at::meta::get_reduction_shape(self, dims_vec, keepdim);

  auto out_dtype = self.scalar_type();
  Tensor output = at::empty(shape, self.options().dtype(out_dtype));
  Tensor indices = at::empty(shape, self.options().dtype(kLong));
  namedinference::propagate_names_for_reduction(
      output, self, dims_vec, keepdim);
  namedinference::propagate_names_for_reduction(
      indices, self, dims_vec, keepdim);

  ReduceIndicesCall(output, indices, self, dim, m);
  return std::make_tuple(output, indices);
}

Tensor MaxAllCall(const Tensor& self, ::musa::dnn::Reduce::Mode m) {
  auto out_dtype = self.scalar_type();
  // torch.max call reudce_all according to out.dim
  Tensor output = at::empty({}, self.options().dtype(out_dtype));
  DimVector dims_vec(0);
  if (self.numel() == 0) {
    output.zero_();
  } else {
    ReduceCall(output, self, dims_vec, m);
  }
  return output;
}

Tensor MaxAll(const Tensor& self) {
  // TODO(@caizhi): use musa porting to instead putting to cpu.
  c10::musa::MUSAGuard device_guard(self.device());
  if (self.scalar_type() == ScalarType::Double) {
    return at::max(self.to("cpu")).to("musa");
  }
  return MaxAllCall(self, ::musa::dnn::Reduce::Mode::MAX);
}

std::tuple<Tensor, Tensor> MaxDim(
    const Tensor& self,
    int64_t dim,
    bool keepdim) {
  return ReductionIndices(self, dim, keepdim, ::musa::dnn::Reduce::Mode::MAX);
}

std::tuple<Tensor&, Tensor&> MaxDimMax(
    const Tensor& self,
    int64_t dim,
    bool keepdim,
    Tensor& output,
    Tensor& indices) {
  dim = maybe_wrap_dim(dim, self.dim());
  IntArrayRef dims(dim);
  DimVector dims_vec(dims);
  maybe_wrap_dims(dims_vec, self.dim());
  auto shape = at::meta::get_reduction_shape(self, dims_vec, keepdim);
  if (0 == output.numel()) {
    at::native::resize_output(output, shape);
  }
  if (0 == indices.numel()) {
    at::native::resize_output(indices, shape);
  }
  ReduceIndicesCall(output, indices, self, dim, ::musa::dnn::Reduce::Mode::MAX);
  return std::tuple<Tensor&, Tensor&>(output, indices);
}

std::tuple<Tensor, Tensor> MaxNamesDim(
    const Tensor& self,
    Dimname dim,
    bool keepdim) {
  return ReductionIndices(
      self,
      dimname_to_position(self, dim),
      keepdim,
      ::musa::dnn::Reduce::Mode::MAX);
}

std::tuple<Tensor&, Tensor&> MaxNamesDimMax(
    const Tensor& self,
    Dimname dim,
    bool keepdim,
    Tensor& output,
    Tensor& indices) {
  UNUSED(keepdim);
  ReduceIndicesCall(
      output,
      indices,
      self,
      dimname_to_position(self, dim),
      ::musa::dnn::Reduce::Mode::MAX);
  return std::tuple<Tensor&, Tensor&>(output, indices);
}

Tensor All(const Tensor& self) {
  Tensor output = Reduction(
      self,
      IntArrayRef{},
      false,
      self.scalar_type(),
      ::musa::dnn::Reduce::Mode::AND);
  if (self.scalar_type() != ScalarType::Bool) {
    output = output.to(ScalarType::Bool);
  }
  return output;
}

Tensor AllDim(const Tensor& self, int64_t dim, bool keepdim) {
  TORCH_CHECK(
      self.scalar_type() == ScalarType::Bool ||
          self.scalar_type() == ScalarType::Byte || self.item<int>() == 0 ||
          self.item<int>() == 1,
      concatenate(
          "Now only support bool/uint8 type or 0/1 value, but got ",
          self.scalar_type(),
          self));
  IntArrayRef dims(dim);
  if (self.scalar_type() == ScalarType::Byte) {
    Tensor self_;
    self_ = self.to(ScalarType::Bool);
    return Reduction(
        self_,
        {dim},
        keepdim,
        self.scalar_type(),
        ::musa::dnn::Reduce::Mode::AND);
  } else {
    return Reduction(
        self,
        {dim},
        keepdim,
        self.scalar_type(),
        ::musa::dnn::Reduce::Mode::AND);
  }
}

Tensor& AllDimOut(const Tensor& self, int64_t dim, bool keepdim, Tensor& out) {
  UNUSED(keepdim);
  TORCH_CHECK(
      self.scalar_type() == ScalarType::Bool ||
          self.scalar_type() == ScalarType::Byte || self.item<int>() == 0 ||
          self.item<int>() == 1,
      concatenate(
          "Now only support bool/uint8 type or 0/1 value, but got ",
          self.scalar_type(),
          self));
  IntArrayRef dims(dim);
  if (self.scalar_type() == ScalarType::Byte) {
    Tensor self_;
    self_ = self.to(ScalarType::Bool);
    ReduceCall(out, self_, dims, ::musa::dnn::Reduce::Mode::AND);
  } else {
    ReduceCall(out, self, dims, ::musa::dnn::Reduce::Mode::AND);
  }

  return out;
}
void ArgMinOrMaxOutTemplate(
    const Tensor& self,
    c10::optional<int64_t> dim,
    Tensor& result,
    ::musa::dnn::Reduce::Mode m) {
  Tensor self_ = dim.has_value() ? self : self.flatten();
  auto dim_ = dim.has_value() ? maybe_wrap_dim(dim.value(), self.dim()) : 0;
  ReduceIndicesOnlyCall(result, self_, dim_, m);
}

TORCH_IMPL_FUNC(argmax_out_musa)
(const Tensor& self,
 c10::optional<int64_t> dim,
 bool keepdim,
 const Tensor& result) {
  (void)keepdim;
  ArgMinOrMaxOutTemplate(
      self, dim, const_cast<Tensor&>(result), ::musa::dnn::Reduce::Mode::MAX);
}

TORCH_IMPL_FUNC(argmin_out_musa)
(const Tensor& self,
 c10::optional<int64_t> dim,
 bool keepdim,
 const Tensor& result) {
  (void)keepdim;
  ArgMinOrMaxOutTemplate(
      self, dim, const_cast<Tensor&>(result), ::musa::dnn::Reduce::Mode::MIN);
}

Tensor MinAllCall(const Tensor& self, ::musa::dnn::Reduce::Mode m) {
  auto out_dtype = self.scalar_type();
  // torch.min call reudce_all according to out.dim
  Tensor output = at::empty({}, self.options().dtype(out_dtype));
  DimVector dims_vec(0);
  if (self.numel() == 0) {
    output.zero_();
  } else {
    ReduceCall(output, self, dims_vec, m);
  }
  return output;
}

Tensor MinAll(const Tensor& self) {
  // TODO(@caizhi): use musa porting to instead putting to cpu.
  c10::musa::MUSAGuard device_guard(self.device());
  if (self.scalar_type() == ScalarType::Double) {
    return at::min(self.to("cpu")).to("musa");
  }
  return MinAllCall(self, ::musa::dnn::Reduce::Mode::MIN);
}

std::tuple<Tensor, Tensor> MinDim(
    const Tensor& self,
    int64_t dim,
    bool keepdim) {
  return ReductionIndices(self, dim, keepdim, ::musa::dnn::Reduce::Mode::MIN);
}

std::tuple<Tensor&, Tensor&> MinDimMin(
    const Tensor& self,
    int64_t dim,
    bool keepdim,
    Tensor& output,
    Tensor& indices) {
  UNUSED(keepdim);
  ReduceIndicesCall(output, indices, self, dim, ::musa::dnn::Reduce::Mode::MIN);
  return std::tuple<Tensor&, Tensor&>(output, indices);
}

std::tuple<Tensor, Tensor> MinNamesDim(
    const Tensor& self,
    Dimname dim,
    bool keepdim) {
  return ReductionIndices(
      self,
      dimname_to_position(self, dim),
      keepdim,
      ::musa::dnn::Reduce::Mode::MIN);
}

std::tuple<Tensor&, Tensor&> MinNamesDimMin(
    const Tensor& self,
    Dimname dim,
    bool keepdim,
    Tensor& output,
    Tensor& indices) {
  ReduceIndicesCall(
      output,
      indices,
      self,
      dimname_to_position(self, dim),
      ::musa::dnn::Reduce::Mode::MIN);
  return std::tuple<Tensor&, Tensor&>(output, indices);
}

std::tuple<at::Tensor, at::Tensor> VarMeanCorrection(
    const at::Tensor& self,
    at::OptionalIntArrayRef dim,
    const c10::optional<at::Scalar>& correction,
    bool keepdim) {
  // No device check
  torch::utils::musa_lazy_init();
  c10::musa::MUSAGuard device_guard(self.device());
  return at::native::var_mean(self, dim, correction, keepdim);
}

at::Tensor VarCorrection(
    const at::Tensor& self,
    at::OptionalIntArrayRef dim,
    const c10::optional<at::Scalar>& correction,
    bool keepdim) {
  // No device check
  const OptionalDeviceGuard device_guard(device_of(self));
  return at::native::var(self, dim, correction, keepdim);
}

at::Tensor& VarOutCorrection(
    const at::Tensor& self,
    at::OptionalIntArrayRef dim,
    const c10::optional<at::Scalar>& correction,
    bool keepdim,
    at::Tensor& out) {
  // No device check
  const OptionalDeviceGuard device_guard(device_of(self));
  return at::native::var_out(self, dim, correction, keepdim, out);
}

} // namespace musa
} // namespace at
