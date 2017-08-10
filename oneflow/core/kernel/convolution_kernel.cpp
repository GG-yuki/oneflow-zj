#include "oneflow/core/kernel/convolution_kernel.h"
#include "oneflow/core/kernel/kernel_util.h"

namespace oneflow {

namespace {

inline bool IsAGreaterThanZeroAndLessThanB(int a, int b) {
  return static_cast<unsigned>(a) < static_cast<unsigned>(b);
}

}  // namespace

template<typename FloatingPointType>
class ConvolutionKernelUtil<DeviceType::kCPU, FloatingPointType> final {
 public:
  OF_DISALLOW_COPY_AND_MOVE(ConvolutionKernelUtil);
  ConvolutionKernelUtil() = delete;

  static void Col2Im(const KernelCtx& ctx, const FloatingPointType* data_col,
                     const int channels, const int height, const int width,
                     const int kernel_h, const int kernel_w, const int pad_h,
                     const int pad_w, const int stride_h, const int stride_w,
                     const int dilation_h, const int dilation_w,
                     FloatingPointType* mut_dptr) {
    ctx.device_ctx->cpu_stream()->SendWork([=]() mutable {
      memset(mut_dptr, 0,
             height * width * channels * sizeof(FloatingPointType));
      const int output_h =
          (height + 2 * pad_h - (dilation_h * (kernel_h - 1) + 1)) / stride_h
          + 1;
      const int output_w =
          (width + 2 * pad_w - (dilation_w * (kernel_w - 1) + 1)) / stride_w
          + 1;
      const int channel_size = height * width;
      for (int channel = channels; channel--; mut_dptr += channel_size) {
        for (int kernel_row = 0; kernel_row < kernel_h; ++kernel_row) {
          for (int kernel_col = 0; kernel_col < kernel_w; ++kernel_col) {
            int input_row = -pad_h + kernel_row * dilation_h;
            for (int output_rows = output_h; output_rows; --output_rows) {
              if (!IsAGreaterThanZeroAndLessThanB(input_row, height)) {
                data_col += output_w;
              } else {
                int input_col = -pad_w + kernel_col * dilation_w;
                for (int output_col = output_w; output_col; --output_col) {
                  if (IsAGreaterThanZeroAndLessThanB(input_col, width)) {
                    mut_dptr[input_row * width + input_col] += *data_col;
                  }
                  ++data_col;
                  input_col += stride_w;
                }
              }
              input_row += stride_h;
            }
          }
        }
      }
    });
  }

  static void Im2Col(const KernelCtx& ctx, const FloatingPointType* dptr,
                     const int channels, const int height, const int width,
                     const int kernel_h, const int kernel_w, const int pad_h,
                     const int pad_w, const int stride_h, const int stride_w,
                     const int dilation_h, const int dilation_w,
                     FloatingPointType* data_col) {
    ctx.device_ctx->cpu_stream()->SendWork([=]() mutable {
      const int output_h =
          (height + 2 * pad_h - (dilation_h * (kernel_h - 1) + 1)) / stride_h
          + 1;
      const int output_w =
          (width + 2 * pad_w - (dilation_w * (kernel_w - 1) + 1)) / stride_w
          + 1;
      const int channel_size = height * width;
      for (int channel = channels; channel--; dptr += channel_size) {
        for (int kernel_row = 0; kernel_row < kernel_h; ++kernel_row) {
          for (int kernel_col = 0; kernel_col < kernel_w; ++kernel_col) {
            int input_row = -pad_h + kernel_row * dilation_h;
            for (int output_rows = output_h; output_rows; --output_rows) {
              if (!IsAGreaterThanZeroAndLessThanB(input_row, height)) {
                for (int output_cols = output_w; output_cols; --output_cols) {
                  *(data_col++) = 0;
                }
              } else {
                int input_col = -pad_w + kernel_col * dilation_w;
                for (int output_col = output_w; output_col; --output_col) {
                  if (IsAGreaterThanZeroAndLessThanB(input_col, width)) {
                    *(data_col++) = dptr[input_row * width + input_col];
                  } else {
                    *(data_col++) = 0;
                  }
                  input_col += stride_w;
                }
              }
              input_row += stride_h;
            }
          }
        }
      }
    });
  }
};

template<DeviceType device_type, typename FloatingPointType>
void ConvolutionKernel<device_type, FloatingPointType>::Forward(
    const KernelCtx& ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  const Blob* in = BnInOp2Blob("in");
  const Shape& in_shape = in->shape();
  CHECK_EQ(in_shape.NumAxes(), 4);
  Blob* out = BnInOp2Blob("out");
  Blob* col_buf = BnInOp2Blob("col_buf");
  const Blob* weight = BnInOp2Blob("weight");
  const int64_t in_im_sz = in_shape.Count(1);
  const int64_t out_im_sz = out->shape().Count(1);
  const int64_t col_im_sz = col_buf->shape().Count(1);
  auto conv_conf = op()->op_conf().convolution_conf();
  for (size_t i = 0; i < in_shape.At(0); ++i) {
    ConvolutionKernelUtil<device_type, FloatingPointType>::Im2Col(
        ctx, in->dptr<FloatingPointType>() + i * in_im_sz, in_shape.At(1),
        in_shape.At(2), in_shape.At(3), conv_conf.kernel_size(0),
        conv_conf.kernel_size(1), conv_conf.pad(0), conv_conf.pad(1),
        conv_conf.stride(0), conv_conf.stride(1), conv_conf.dilation(0),
        conv_conf.dilation(1),
        col_buf->mut_dptr<FloatingPointType>() + i * col_im_sz);

    KernelUtil<device_type, FloatingPointType>::BlasGemm(
        ctx, CBLAS_ORDER::CblasRowMajor, CblasNoTrans, CblasTrans,
        out->shape().At(1), out->shape().Count(2), weight->shape().At(1),
        static_cast<FloatingPointType>(1.0), weight->dptr<FloatingPointType>(),
        weight->shape().At(1),
        col_buf->dptr<FloatingPointType>() + i * col_im_sz,
        weight->shape().At(1), static_cast<FloatingPointType>(0.0),
        out->mut_dptr<FloatingPointType>() + i * out_im_sz,
        col_buf->shape().At(1));

    if (op()->GetBoolFromSpecialConf("has_bias_term")) {
      const Blob* bias = BnInOp2Blob("bias");
      const Blob* bias_multiplier = BnInOp2Blob("bias_multiplier");

      // out_data = bias * bias_multiplier + out_data
      KernelUtil<device_type, FloatingPointType>::BlasGemm(
          ctx, CBLAS_ORDER::CblasRowMajor, CblasNoTrans, CblasNoTrans,
          bias->shape().At(0), bias_multiplier->shape().At(0), 1,
          static_cast<FloatingPointType>(1.0), bias->dptr<FloatingPointType>(),
          1, bias_multiplier->dptr<FloatingPointType>(),
          bias_multiplier->shape().At(0), static_cast<FloatingPointType>(1.0),
          out->mut_dptr<FloatingPointType>() + i * out_im_sz,
          bias_multiplier->shape().At(0));
    }
  }
}

template<DeviceType device_type, typename FloatingPointType>
void ConvolutionKernel<device_type, FloatingPointType>::ComputeWeightDiff(
    const KernelCtx& ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  Blob* weight_diff = BnInOp2Blob("weight_diff");
  const Blob* col_buf = BnInOp2Blob("col_buf");
  const Blob* out_diff = BnInOp2Blob("out_diff");
  const int64_t out_im_sz = out_diff->shape().Count(1);
  const int64_t batch_sz = out_diff->shape().At(0);
  const int64_t conv_sliding_window_steps = out_diff->shape().Count(2);

  KernelUtil<device_type, FloatingPointType>::Memset(
      ctx, weight_diff->mut_dptr(), 0,
      sizeof(FloatingPointType) * weight_diff->shape().elem_cnt());
  for (size_t i = 0; i < batch_sz; ++i) {
    KernelUtil<device_type, FloatingPointType>::BlasGemm(
        ctx, CBLAS_ORDER::CblasRowMajor, CblasNoTrans, CblasNoTrans,
        weight_diff->shape().At(0), weight_diff->shape().At(1),
        out_diff->shape().Count(2),
        static_cast<FloatingPointType>(1.0) / conv_sliding_window_steps,
        out_diff->dptr<FloatingPointType>() + i * out_im_sz,
        out_diff->shape().Count(2),
        col_buf->dptr<FloatingPointType>() + i * col_buf->shape().Count(1),
        col_buf->shape().At(2), static_cast<FloatingPointType>(1.0),
        weight_diff->mut_dptr<FloatingPointType>(), weight_diff->shape().At(1));
  }
}

template<DeviceType device_type, typename FloatingPointType>
void ConvolutionKernel<device_type, FloatingPointType>::ComputeBiasDiff(
    const KernelCtx& ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  const Blob* out_diff = BnInOp2Blob("out_diff");
  const int64_t out_im_sz = out_diff->shape().Count(1);
  const int64_t batch_sz = out_diff->shape().At(0);
  const Blob* bias_mul = BnInOp2Blob("bias_multiplier");
  Blob* bias_diff = BnInOp2Blob("bias_diff");
  const int64_t conv_sliding_window_steps = out_diff->shape().Count(2);

  KernelUtil<device_type, FloatingPointType>::Memset(
      ctx, bias_diff->mut_dptr(), 0,
      sizeof(FloatingPointType) * bias_diff->shape().elem_cnt());
  for (size_t i = 0; i < batch_sz; ++i) {
    KernelUtil<device_type, FloatingPointType>::BlasGemm(
        ctx, CBLAS_ORDER::CblasRowMajor, CblasNoTrans, CblasNoTrans,
        bias_diff->shape().At(0), 1, bias_mul->shape().At(0),
        static_cast<FloatingPointType>(1.0) / conv_sliding_window_steps,
        out_diff->dptr<FloatingPointType>() + i * out_im_sz,
        out_diff->shape().Count(2), bias_mul->dptr<FloatingPointType>(), 1,
        static_cast<FloatingPointType>(1.0),
        bias_diff->mut_dptr<FloatingPointType>(), 1);
  }
}

template<DeviceType device_type, typename FloatingPointType>
void ConvolutionKernel<device_type, FloatingPointType>::ComputeInputDiff(
    const KernelCtx& ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  Blob* in_diff = BnInOp2Blob("in_diff");
  if (in_diff == nullptr) { return; }

  const Blob* out_diff = BnInOp2Blob("out_diff");
  const Blob* weight = BnInOp2Blob("weight");
  Blob* col_buf = BnInOp2Blob("col_buf");

  const int64_t out_im_sz = out_diff->shape().Count(1);
  const int64_t batch_sz = out_diff->shape().At(0);
  for (size_t i = 0; i < batch_sz; ++i) {
    KernelUtil<device_type, FloatingPointType>::BlasGemm(
        ctx, CBLAS_ORDER::CblasRowMajor, CblasTrans, CblasNoTrans,
        col_buf->shape().At(1), col_buf->shape().At(2), weight->shape().At(0),
        static_cast<FloatingPointType>(1.0),
        out_diff->dptr<FloatingPointType>() + i * out_im_sz,
        out_diff->shape().Count(2), weight->dptr<FloatingPointType>(),
        weight->shape().At(1), static_cast<FloatingPointType>(0.0),
        col_buf->mut_dptr<FloatingPointType>() + i * col_buf->shape().Count(1),
        col_buf->shape().At(2));
  }

  const Shape& in_diff_shape = in_diff->shape();
  auto conv_conf = op()->op_conf().convolution_conf();
  for (size_t i = 0; i < batch_sz; ++i) {
    ConvolutionKernelUtil<device_type, FloatingPointType>::Col2Im(
        ctx, col_buf->dptr<FloatingPointType>() + i * col_buf->shape().Count(1),
        in_diff_shape.At(1), in_diff_shape.At(2), in_diff_shape.At(3),
        conv_conf.kernel_size(0), conv_conf.kernel_size(1), conv_conf.pad(0),
        conv_conf.pad(1), conv_conf.stride(0), conv_conf.stride(1),
        conv_conf.dilation(0), conv_conf.dilation(1),
        in_diff->mut_dptr<FloatingPointType>() + i * in_diff_shape.Count(1));
  }
}

template<DeviceType device_type, typename FloatingPointType>
void ConvolutionKernel<device_type, FloatingPointType>::Backward(
    const KernelCtx& ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  ComputeWeightDiff(ctx, BnInOp2Blob);
  if (op()->GetBoolFromSpecialConf("has_bias_term")) {
    ComputeBiasDiff(ctx, BnInOp2Blob);
  }
  ComputeInputDiff(ctx, BnInOp2Blob);
}

template<DeviceType device_type, typename FloatingPointType>
void ConvolutionKernel<device_type, FloatingPointType>::
    InitModelBlobsWithRandomSeed(
        const KernelCtx& ctx, std::mt19937 random_seed_gen,
        std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  KernelUtil<device_type, FloatingPointType>::FillWithProperConf(
      ctx, OF_PB_POINTER_GET(op()->op_conf().convolution_conf(), weight_fill),
      random_seed_gen(), BnInOp2Blob("weight"));

  if (op()->GetBoolFromSpecialConf("has_bias_term")) {
    KernelUtil<device_type, FloatingPointType>::FillWithProperConf(
        ctx, OF_PB_POINTER_GET(op()->op_conf().convolution_conf(), bias_fill),
        random_seed_gen(), BnInOp2Blob("bias"));
  }
}

template<DeviceType device_type, typename FloatingPointType>
void ConvolutionKernel<device_type, FloatingPointType>::InitModelTmpBlobs(
    const KernelCtx& ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  if (op()->GetBoolFromSpecialConf("has_bias_term")) {
    FillConf bias_multiplier_fill_conf;
    bias_multiplier_fill_conf.mutable_constant_conf()->set_value(1.0f);
    KernelUtil<device_type, FloatingPointType>::Fill(
        ctx, bias_multiplier_fill_conf, 0, BnInOp2Blob("bias_multiplier"));
  }
}

INSTANTIATE_KERNEL_CLASS(ConvolutionKernel);
INSTANTIATE_CPU_KERNEL_UTIL_CLASS(ConvolutionKernelUtil);
REGISTER_CPU_KERNEL(OperatorConf::kConvolutionConf, ConvolutionKernel);

}  // namespace oneflow
