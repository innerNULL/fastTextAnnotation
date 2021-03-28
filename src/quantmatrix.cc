/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "quantmatrix.h"

#include <assert.h>
#include <iostream>
#include <stdexcept>

namespace fasttext {

QuantMatrix::QuantMatrix() : Matrix(), qnorm_(false), codesize_(0) {}

QuantMatrix::QuantMatrix(DenseMatrix&& mat, int32_t dsub, bool qnorm)
    : Matrix(mat.size(0), mat.size(1)),
      qnorm_(qnorm),
      codesize_(mat.size(0) * ((mat.size(1) + dsub - 1) / dsub)) {
  codes_.resize(codesize_);
  pq_ = std::unique_ptr<ProductQuantizer>(new ProductQuantizer(n_, dsub));
  if (qnorm_) {
    norm_codes_.resize(m_);
    npq_ = std::unique_ptr<ProductQuantizer>(new ProductQuantizer(1, 1));
  }
  quantize(std::forward<DenseMatrix>(mat));
}

/**
 * @brief
 * This is sort of executing product-quantization on an 1-dimension 
 * l2-norm vector, each element of this vector represents an l2-norm 
 * value for one embedding in embedding matrix. 
 *
 * This may sound wierd but not very hard to understand, for example, 
 * the k-means process in product-quantization is just run on several 
 * points located on an 1-dim space (axis).
 *
 * TODO: My guess is, the reason to do this when we needs to add 
 * normalization step in PQ process, the qunatized l2-norm vector 
 * can keep certain "computational consistancy" with the following 
 * quantized embedding matrix. You know, the following quantized 
 * embedding matrix is an `QuantMatrix` object, and only an 
 * `QuantMatrix` object can doing some mathematical opration with it. 
 * BUT, the quantization of l2-norm vector and embedding-matrix do 
 * not have to share the same product-quantization parameters, such 
 * as subquantizer number. 
 */
void QuantMatrix::quantizeNorm(const Vector& norms) {
  assert(qnorm_);
  assert(norms.size() == m_);
  auto dataptr = norms.data();
  npq_->train(m_, dataptr); // `m_` is the size of `norms` vector.
  npq_->compute_codes(dataptr, norm_codes_.data(), m_);
}

/**
 * @brief 
 * Executing the Product-Quantization on an matrix `mat`.
 */
void QuantMatrix::quantize(DenseMatrix&& mat) {
  if (qnorm_) {
    /// The following tow line calculating each row's l2-norm for  
    /// input matrix `mat` and saving the results into `norms`. 
    Vector norms(mat.size(0));
    mat.l2NormRow(norms);
    /// Normalizing each row of `mat` by dividing each row with its 
    /// corresponding l2-norm.
    mat.divideRow(norms);
    /// Training the product-quantizer for embedding matrix's l2-norm vector
    quantizeNorm(norms);
  }
  auto dataptr = mat.data();
  pq_->train(m_, dataptr);
  pq_->compute_codes(dataptr, codes_.data(), m_);
}

real QuantMatrix::dotRow(const Vector& vec, int64_t i) const {
  assert(i >= 0);
  assert(i < m_);
  assert(vec.size() == n_);
  real norm = 1;
  if (qnorm_) {
    norm = npq_->get_centroids(0, norm_codes_[i])[0];
  }
  return pq_->mulcode(vec, codes_.data(), i, norm);
}

void QuantMatrix::addVectorToRow(const Vector&, int64_t, real) {
  throw std::runtime_error("Operation not permitted on quantized matrices.");
}

void QuantMatrix::addRowToVector(Vector& x, int32_t i, real a) const {
  real norm = 1;
  if (qnorm_) {
    norm = npq_->get_centroids(0, norm_codes_[i])[0];
  }
  pq_->addcode(x, codes_.data(), i, a * norm);
}

void QuantMatrix::addRowToVector(Vector& x, int32_t i) const {
  real norm = 1;
  if (qnorm_) {
    norm = npq_->get_centroids(0, norm_codes_[i])[0];
  }
  pq_->addcode(x, codes_.data(), i, norm);
}

void QuantMatrix::save(std::ostream& out) const {
  out.write((char*)&qnorm_, sizeof(qnorm_));
  out.write((char*)&m_, sizeof(m_));
  out.write((char*)&n_, sizeof(n_));
  out.write((char*)&codesize_, sizeof(codesize_));
  out.write((char*)codes_.data(), codesize_ * sizeof(uint8_t));
  pq_->save(out);
  if (qnorm_) {
    out.write((char*)norm_codes_.data(), m_ * sizeof(uint8_t));
    npq_->save(out);
  }
}

void QuantMatrix::load(std::istream& in) {
  in.read((char*)&qnorm_, sizeof(qnorm_));
  in.read((char*)&m_, sizeof(m_));
  in.read((char*)&n_, sizeof(n_));
  in.read((char*)&codesize_, sizeof(codesize_));
  codes_ = std::vector<uint8_t>(codesize_);
  in.read((char*)codes_.data(), codesize_ * sizeof(uint8_t));
  pq_ = std::unique_ptr<ProductQuantizer>(new ProductQuantizer());
  pq_->load(in);
  if (qnorm_) {
    norm_codes_ = std::vector<uint8_t>(m_);
    in.read((char*)norm_codes_.data(), m_ * sizeof(uint8_t));
    npq_ = std::unique_ptr<ProductQuantizer>(new ProductQuantizer());
    npq_->load(in);
  }
}

void QuantMatrix::dump(std::ostream&) const {
  throw std::runtime_error("Operation not permitted on quantized matrices.");
}

} // namespace fasttext
