
#include <ATen/FunctionalInverses.h>

#include <ATen/ATen.h>
#include <ATen/ExpandUtils.h>
namespace at {
namespace functionalization {
namespace impl {

// This logic is similar to autograd code for view backwards calls.
// We can't easily share it though, because (eventually) these functions
// will all call `permute/unsqueeze_copy()` instead of `permute/unsqueeze`.

Tensor permute_inverse(const Tensor& self, IntArrayRef dims) {
  // invert the permutation
  auto ndims = dims.size();
  std::vector<int64_t> dims_(ndims);
  for(const auto i : c10::irange(ndims)) {
    dims_[at::maybe_wrap_dim(dims[i], ndims)] = i;
  }
  return self.permute(dims_);
}

Tensor unsqueeze_to(const Tensor & self, IntArrayRef sizes) {
  auto result = self;

  int64_t nDims = sizes.size();
  for(const auto dim : c10::irange(nDims)) {
    if (sizes[dim] == 1) {
      result = result.unsqueeze(dim);
    }
  }
  return result;
}

Tensor unsqueeze_to(const Tensor & self, int64_t dim, IntArrayRef sizes) {
  dim = at::maybe_wrap_dim(dim, sizes.size());
  // in NumPy it's not an error to unsqueeze a scalar, but we still need to avoided
  // unsqueezing in the backward.
  if (sizes.size() > 0 && sizes[dim] == 1) {
    return self.unsqueeze(dim);
  }
  return self;
}

// Note [Functionalization Pass: View Inverses].
// This file contains the implementation of each "view inverse".
// These aren't really true inverses in the mathematically sense: each view inverse describes how to undo
// the original view (although it takes in different arguments).
//
// In general, view inverses respect the following property:
//
// b = view1(a, args...)
// a_copy = view1_inverse(a, b, args...)
// a and a_copy should be equal.
//
// Note that a is also passed as an argument to view1_inverse in the above example.
// This isn't actually required for most view operators: it's only required for view ops
// where you can't figure out what the size of the base tensor is given just the view tensor and arguments.
// Examples are slice/select/scatter/squeeze/as_strided.
// We happen to be passing in the base tensor in all cases, mostly to make the codegen simpler.
// But you'll see below that the "base" argument is ignored by most view_inverse implementations.

// ----------------------------------------------------------
// Implementations of each view_inverse() function are below.
// One of these needs to be implemented for every existing non-composite view operator.
// The codegen automatically generates the corresponding function declaration.
// ----------------------------------------------------------

Tensor _fw_primal_inverse(const at::Tensor& base, const at::Tensor& mutated_view, int64_t level) {
    TORCH_INTERNAL_ASSERT(false, "Attempted to call _fw_primal() during the functionalization pass. For now, this is not supported.");
    return Tensor();
}

Tensor view_as_real_inverse(const Tensor& base, const Tensor& mutated_view) {
    return at::view_as_complex(mutated_view);
}

Tensor view_as_complex_inverse(const Tensor& base, const Tensor& mutated_view) {
    return at::view_as_real(mutated_view.resolve_conj());
}

Tensor _conj_inverse(const Tensor& base, const Tensor& mutated_view) {
    return mutated_view.conj();
}

Tensor _neg_view_inverse(const Tensor& base, const Tensor& mutated_view) {
    return mutated_view.neg();
}

Tensor as_strided_inverse(const Tensor& base, const Tensor& mutated_view, at::IntArrayRef size, at::IntArrayRef stride, c10::optional<int64_t> storage_offset) {
    TORCH_INTERNAL_ASSERT(false, "as_strided has not been implemented in the functionalization pass yet");
    return Tensor();
}

Tensor diagonal_inverse(const Tensor& base, const Tensor& mutated_view, int64_t offset, int64_t dim1, int64_t dim2) {
    return base.diagonal_scatter(mutated_view, offset, dim1, dim2);
}

Tensor expand_inverse(const Tensor& base, const Tensor& mutated_view, at::IntArrayRef size, bool implicit) {
    return at::sum_to(mutated_view, base.sizes());
}

Tensor permute_inverse(const Tensor& base, const Tensor& mutated_view, at::IntArrayRef dims) {
    return permute_inverse(mutated_view, dims);
}

Tensor _reshape_alias_inverse(const Tensor& base, const Tensor& mutated_view, at::IntArrayRef size, at::IntArrayRef stride) {
    return mutated_view._reshape_alias(base.sizes(), base.strides());
}

Tensor select_inverse(const Tensor& base, const Tensor& mutated_view, int64_t dim, int64_t index) {
    return base.select_scatter(mutated_view, dim, index);
}
Tensor detach_inverse(const Tensor& base, const Tensor& mutated_view) {
    // the functionalization pass doesn't care about autograd metadata - as a view, I think detach() is just an identity function
    return mutated_view;
}

Tensor slice_inverse(const Tensor& base, const Tensor& mutated_view, int64_t dim, c10::optional<int64_t> start, c10::optional<int64_t> end, int64_t step) {
    return base.slice_scatter(mutated_view, dim, start, end, step);
}

Tensor split_inverse(const Tensor& base, const Tensor& mutated_view, int64_t mutated_view_idx, int64_t split_size, int64_t dim) {
    // It would be nice if this logic could be re-used from autograd's split_backward(), but I don't think it can.
    // For functionalization, we have only have one of the tensors from the TensorList outputed by split(), and we want to layer i
    // on top of the base tensor.
    // For autograd, we have all of the tensors outputted by split() and we just want to stack them.
    dim = at::maybe_wrap_dim(dim, base.sizes().size());
    auto dim_size = base.size(dim);
    auto start = mutated_view_idx * split_size;
    auto end = start + split_size;
    if (end > dim_size) end = dim_size;
    return base.slice_scatter(mutated_view, dim, start, end, 1);
}

Tensor split_with_sizes_inverse(const Tensor& base, const Tensor& mutated_view, int64_t mutated_view_idx, at::IntArrayRef split_sizes, int64_t dim) {
    dim = at::maybe_wrap_dim(dim, base.sizes().size());
    auto dim_size = base.size(dim);
    int64_t start = 0;
    for (auto i = 0; i < mutated_view_idx; ++i) {
        start += split_sizes[i];
    }
    auto end = start + split_sizes[mutated_view_idx];
    if (end > dim_size) end = dim_size;
    return base.slice_scatter(mutated_view, dim, start, end, 1);
}

Tensor squeeze_inverse(const Tensor& base, const Tensor& mutated_view) {
    return unsqueeze_to(mutated_view, base.sizes());
}

Tensor squeeze_inverse(const Tensor& base, const Tensor& mutated_view, int64_t dim) {
    return unsqueeze_to(mutated_view, dim, base.sizes());
}

Tensor t_inverse(const Tensor& base, const Tensor& mutated_view) {
    return mutated_view.t();
}

Tensor transpose_inverse(const Tensor& base, const Tensor& mutated_view, int64_t dim0, int64_t dim1) {
    return mutated_view.transpose(dim0, dim1);
}

Tensor unsqueeze_inverse(const Tensor& base, const Tensor& mutated_view, int64_t dim) {
    return mutated_view.squeeze(dim);
}

Tensor _indices_inverse(const Tensor& base, const Tensor& mutated_view) {
    TORCH_INTERNAL_ASSERT(false, "Attempted to call _indices() during the functionalization pass. For now, sparse tensors aren't supported during functionalization");
    return Tensor();
}

Tensor _values_inverse(const Tensor& base, const Tensor& mutated_view) {
    TORCH_INTERNAL_ASSERT(false, "Attempted to call _values() during the functionalization pass. For now, sparse tensors aren't supported during functionalization");
    return Tensor();
}

Tensor indices_inverse(const Tensor& base, const Tensor& mutated_view) {
    TORCH_INTERNAL_ASSERT(false, "Attempted to call indices() during the functionalization pass. For now, sparse tensors aren't supported during functionalization");
    return Tensor();
}

Tensor values_inverse(const Tensor& base, const Tensor& mutated_view) {
    TORCH_INTERNAL_ASSERT(false, "Attempted to call values() during the functionalization pass. For now, sparse tensors aren't supported during functionalization");
    return Tensor();
}

Tensor crow_indices_inverse(const at::Tensor& base, const at::Tensor& mutated_view) {
    TORCH_INTERNAL_ASSERT(false, "Attempted to call crow_indices() during the functionalization pass. For now, sparse tensors aren't supported during functionalization");
    return Tensor();
}

Tensor col_indices_inverse(const at::Tensor& base, const at::Tensor& mutated_view) {
    TORCH_INTERNAL_ASSERT(false, "Attempted to call col_indices() during the functionalization pass. For now, sparse tensors aren't supported during functionalization");
    return Tensor();
}

Tensor unbind_inverse(const Tensor& base, const Tensor& mutated_view, int64_t mutated_view_idx, int64_t dim) {
    dim = at::maybe_wrap_dim(dim, base.sizes().size());
    return base.select_scatter(mutated_view, dim, mutated_view_idx);
}

Tensor view_inverse(const Tensor& base, const Tensor& mutated_view, at::IntArrayRef size) {
    return mutated_view.view(base.sizes());
}

Tensor view_inverse(const Tensor& base, const Tensor& mutated_view, at::ScalarType dtype) {
    return mutated_view.view(base.scalar_type());
}

Tensor unfold_inverse(const Tensor& base, const Tensor& mutated_view, int64_t dimension, int64_t size, int64_t step) {
    // I think autograd and the functionalization pass want the exact same thing here, but need to test to confirm.
    return unfold_backward(mutated_view, base.sizes(), dimension, size, step);
}

Tensor alias_inverse(const Tensor& base, const Tensor& mutated_view) {
    return mutated_view;
}

} // impl
} // functionalization
} // at
