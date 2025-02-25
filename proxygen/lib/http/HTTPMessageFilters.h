/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Memory.h>
#include <folly/io/async/DestructorCheck.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

namespace proxygen {

static const std::string kMessageFilterDefaultName_ = "Unknown";

class HTTPMessageFilter
    : public HTTPTransaction::Handler
    , public folly::DestructorCheck {
 public:
  void setNextTransactionHandler(HTTPTransaction::Handler* next) {
    nextTransactionHandler_ = CHECK_NOTNULL(next);
  }
  virtual void setPrevFilter(HTTPMessageFilter* prev) noexcept {
    prev_ = CHECK_NOTNULL(prev);
  }
  virtual void setPrevTxn(HTTPTransaction* prev) noexcept {
    prev_ = CHECK_NOTNULL(prev);
  }
  HTTPTransaction::Handler* getNextTransactionHandler() {
    return nextTransactionHandler_;
  }

  virtual std::unique_ptr<HTTPMessageFilter> clone() noexcept = 0;

  // These HTTPTransaction::Handler callbacks may be overwritten
  // The default behavior is to pass the call through.
  void onHeadersComplete(std::unique_ptr<HTTPMessage> msg) noexcept override {
    nextTransactionHandler_->onHeadersComplete(std::move(msg));
  }
  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    nextTransactionHandler_->onBody(std::move(chain));
  }
  void onChunkHeader(size_t length) noexcept override {
    nextTransactionHandler_->onChunkHeader(length);
  }
  void onChunkComplete() noexcept override {
    nextTransactionHandler_->onChunkComplete();
  }
  void onTrailers(std::unique_ptr<HTTPHeaders> trailers) noexcept override {
    nextTransactionHandler_->onTrailers(std::move(trailers));
  }
  void onEOM() noexcept override {
    nextTransactionHandler_->onEOM();
  }
  void onUpgrade(UpgradeProtocol protocol) noexcept override {
    nextTransactionHandler_->onUpgrade(protocol);
  }
  void onError(const HTTPException& error) noexcept override {
    nextTransactionHandler_->onError(error);
  }

  // These HTTPTransaction::Handler callbacks cannot be overrwritten
  void setTransaction(HTTPTransaction* txn) noexcept final {
    nextTransactionHandler_->setTransaction(txn);
  }
  void detachTransaction() noexcept final {
    if (prev_.which() == 1) {
      // After detachTransaction(), the HTTPTransaction will destruct itself.
      // Set it to nullptr to avoid holding a stale pointer.
      prev_ = static_cast<HTTPTransaction*>(nullptr);
    }
    if (nextTransactionHandler_) {
      nextTransactionHandler_->detachTransaction();
    }
  }
  void onEgressPaused() noexcept final {
    nextTransactionHandler_->onEgressPaused();
  }
  void onEgressResumed() noexcept final {
    nextTransactionHandler_->onEgressResumed();
  }
  void onPushedTransaction(HTTPTransaction* txn) noexcept final {
    nextTransactionHandler_->onPushedTransaction(txn);
  }
  void onExTransaction(HTTPTransaction* txn) noexcept final {
    nextTransactionHandler_->onExTransaction(txn);
  }

  virtual const std::string& getFilterName() const noexcept {
    return kMessageFilterDefaultName_;
  }

  virtual void pause() noexcept;

  virtual void resume(uint64_t offset) noexcept;

  // This is called by the handler when it wants to detach from the transaction.
  // After this call, the handler and the transaction can be destroyed without
  // notifying each other. We pass the call through the filter chain to
  // avoid holding a stale pointer to the transaction.
  void detachHandlerFromTransaction() noexcept {
    if (prev_.which() == 0) {
      auto prev = boost::get<HTTPMessageFilter*>(prev_);
      if (prev) {
        // prev points to another filter, popagate the call.
        prev->detachHandlerFromTransaction();
      }
    } else {
      auto prev = boost::get<HTTPTransaction*>(prev_);
      if (prev) {
        // prev points to the transaction, detach the handler from the
        // transaction.
        prev->setHandler(nullptr);
        // Set the pointer to nullptr. It is not safe to use the pointer since
        // after this the transaction can be destroyed without notifying the
        // filter.
        prev_ = static_cast<HTTPTransaction*>(nullptr);
      }
    }
  }

 protected:
  virtual void nextOnHeadersComplete(std::unique_ptr<HTTPMessage> msg) {
    nextTransactionHandler_->onHeadersComplete(std::move(msg));
  }
  virtual void nextOnBody(std::unique_ptr<folly::IOBuf> chain) {
    nextTransactionHandler_->onBody(std::move(chain));
  }
  virtual void nextOnChunkHeader(size_t length) {
    nextTransactionHandler_->onChunkHeader(length);
  }
  virtual void nextOnChunkComplete() {
    nextTransactionHandler_->onChunkComplete();
  }
  virtual void nextOnTrailers(std::unique_ptr<HTTPHeaders> trailers) {
    nextTransactionHandler_->onTrailers(std::move(trailers));
  }
  virtual void nextOnEOM() {
    nextTransactionHandler_->onEOM();
  }
  virtual void nextOnError(const HTTPException& ex) {
    nextTransactionHandler_->onError(ex);
  }
  HTTPTransaction::Handler* nextTransactionHandler_{nullptr};

  boost::variant<HTTPMessageFilter*, HTTPTransaction*> prev_ =
      static_cast<HTTPTransaction*>(nullptr);

  bool nextElementIsPaused_{false};
};

} // namespace proxygen
