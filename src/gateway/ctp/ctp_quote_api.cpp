// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#include "gateway/ctp/ctp_quote_api.h"

#include <spdlog/spdlog.h>

#include <utility>

namespace ft {

CtpQuoteApi::CtpQuoteApi(TradingEngineInterface *engine) : engine_(engine) {}

CtpQuoteApi::~CtpQuoteApi() {
  is_error_ = true;
  logout();
}

bool CtpQuoteApi::login(const Config &config) {
  if (is_logon_) {
    spdlog::error("[CtpQuoteApi::login] Don't login twice");
    return true;
  }

  quote_api_.reset(CThostFtdcMdApi::CreateFtdcMdApi());
  if (!quote_api_) {
    spdlog::error("[CtpQuoteApi::login] Failed to create CTP MD API");
    return false;
  }

  server_addr_ = config.quote_server_address;
  broker_id_ = config.broker_id;
  investor_id_ = config.investor_id;
  passwd_ = config.password;

  quote_api_->RegisterSpi(this);
  quote_api_->RegisterFront(const_cast<char *>(server_addr_.c_str()));
  quote_api_->Init();

  for (;;) {
    if (is_error_) return false;

    if (is_connected_) break;
  }

  CThostFtdcReqUserLoginField login_req{};
  strncpy(login_req.BrokerID, broker_id_.c_str(), sizeof(login_req.BrokerID));
  strncpy(login_req.UserID, investor_id_.c_str(), sizeof(login_req.UserID));
  strncpy(login_req.Password, passwd_.c_str(), sizeof(login_req.Password));
  if (quote_api_->ReqUserLogin(&login_req, next_req_id()) != 0) {
    spdlog::error("[CtpQuoteApi::login] Invalid user-login field");
    return false;
  }

  for (;;) {
    if (is_error_) return false;

    if (is_logon_) break;
  }

  std::vector<char *> sub_list;
  sub_list_ = config.subscription_list;

  for (const auto &p : sub_list_)
    sub_list.emplace_back(const_cast<char *>(p.c_str()));

  if (sub_list.size() > 0) {
    if (quote_api_->SubscribeMarketData(sub_list.data(), sub_list.size()) !=
        0) {
      spdlog::error("[CtpQuoteApi::login] Failed to subscribe");
      return false;
    }
  }

  return true;
}

void CtpQuoteApi::logout() {
  if (is_logon_) {
    CThostFtdcUserLogoutField req{};
    strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
    strncpy(req.UserID, investor_id_.c_str(), sizeof(req.UserID));
    if (quote_api_->ReqUserLogout(&req, next_req_id()) != 0) return;

    while (is_logon_) continue;
  }
}

void CtpQuoteApi::OnFrontConnected() {
  is_connected_ = true;
  spdlog::debug("[CtpQuoteApi::OnFrontConnectedMD] Connected");
}

void CtpQuoteApi::OnFrontDisconnected(int reason) {
  is_error_ = true;
  is_connected_ = false;
  spdlog::error("[CtpQuoteApi::OnFrontDisconnectedMD] Disconnected");
}

void CtpQuoteApi::OnHeartBeatWarning(int time_lapse) {
  spdlog::warn(
      "[CtpQuoteApi::OnHeartBeatWarningMD] Warn. No packet received for a "
      "period of time");
}

void CtpQuoteApi::OnRspUserLogin(CThostFtdcRspUserLoginField *login_rsp,
                                 CThostFtdcRspInfoField *rsp_info, int req_id,
                                 bool is_last) {
  if (!is_last) return;

  if (is_error_rsp(rsp_info)) {
    spdlog::error("[CtpQuoteApi::OnRspUserLogin] Failed. ErrorMsg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    is_error_ = true;
    return;
  }

  spdlog::debug("[CtpQuoteApi::OnRspUserLogin] Success. Login as {}",
                investor_id_);
  is_logon_ = true;
}

void CtpQuoteApi::OnRspUserLogout(CThostFtdcUserLogoutField *logout_rsp,
                                  CThostFtdcRspInfoField *rsp_info, int req_id,
                                  bool is_last) {
  spdlog::debug(
      "[CtpQuoteApi::OnRspUserLogout] Success. Broker ID: {}, Investor ID: {}",
      logout_rsp->BrokerID, logout_rsp->UserID);
  is_logon_ = false;
}

void CtpQuoteApi::OnRspError(CThostFtdcRspInfoField *rsp_info, int req_id,
                             bool is_last) {
  spdlog::debug("[CtpQuoteApi::OnRspError] ErrorMsg: {}",
                gb2312_to_utf8(rsp_info->ErrorMsg));
  is_logon_ = false;
}

void CtpQuoteApi::OnRspSubMarketData(
    CThostFtdcSpecificInstrumentField *instrument,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {
  if (is_error_rsp(rsp_info) || !instrument) {
    spdlog::error("[CtpQuoteApi::OnRspSubMarketData] Failed. Error Msg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    return;
  }

  auto contract = ContractTable::get_by_ticker(instrument->InstrumentID);
  if (!contract) {
    spdlog::error(
        "[CtpQuoteApi::OnRspSubMarketData] ExchangeID not found in contract "
        "list. "
        "Maybe you should update the contract list. Ticker: {}",
        instrument->InstrumentID);
    return;
  }

  spdlog::debug("[CtpQuoteApi::OnRspSubMarketData] Success. Ticker: {}",
                contract->ticker);
}

void CtpQuoteApi::OnRspUnSubMarketData(
    CThostFtdcSpecificInstrumentField *instrument,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {}

void CtpQuoteApi::OnRspSubForQuoteRsp(
    CThostFtdcSpecificInstrumentField *instrument,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {}

void CtpQuoteApi::OnRspUnSubForQuoteRsp(
    CThostFtdcSpecificInstrumentField *instrument,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {}

void CtpQuoteApi::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *md) {
  if (!md) {
    spdlog::error("[CtpQuoteApi::OnRtnDepthMarketData] Failed. md is nullptr");
    return;
  }

  auto contract = ContractTable::get_by_ticker(md->InstrumentID);
  if (!contract) {
    spdlog::warn(
        "[CtpQuoteApi::OnRtnDepthMarketData] Failed. ExchangeID not found in "
        "contract list. "
        "Maybe you should update the contract list. Symbol: {}",
        md->InstrumentID);
    return;
  }

  TickData tick{};
  tick.ticker_index = contract->index;

  struct tm _tm;
  strptime(md->UpdateTime, "%H:%M:%S", &_tm);
  tick.time_sec = _tm.tm_sec + _tm.tm_min * 60 + _tm.tm_hour * 3600;
  tick.time_ms = md->UpdateMillisec;
  // tick.date = md->ActionDay;

  tick.volume = md->Volume;
  tick.turnover = md->Turnover;
  tick.open_interest = md->OpenInterest;
  tick.last_price = adjust_price(md->LastPrice);
  tick.open_price = adjust_price(md->OpenPrice);
  tick.highest_price = adjust_price(md->HighestPrice);
  tick.lowest_price = adjust_price(md->LowestPrice);
  tick.pre_close_price = adjust_price(md->PreClosePrice);
  tick.upper_limit_price = adjust_price(md->UpperLimitPrice);
  tick.lower_limit_price = adjust_price(md->LowerLimitPrice);

  tick.level = 5;
  tick.ask[0] = adjust_price(md->AskPrice1);
  tick.ask[1] = adjust_price(md->AskPrice2);
  tick.ask[2] = adjust_price(md->AskPrice3);
  tick.ask[3] = adjust_price(md->AskPrice4);
  tick.ask[4] = adjust_price(md->AskPrice5);
  tick.bid[0] = adjust_price(md->BidPrice1);
  tick.bid[1] = adjust_price(md->BidPrice2);
  tick.bid[2] = adjust_price(md->BidPrice3);
  tick.bid[3] = adjust_price(md->BidPrice4);
  tick.bid[4] = adjust_price(md->BidPrice5);
  tick.ask_volume[0] = md->AskVolume1;
  tick.ask_volume[1] = md->AskVolume2;
  tick.ask_volume[2] = md->AskVolume3;
  tick.ask_volume[3] = md->AskVolume4;
  tick.ask_volume[4] = md->AskVolume5;
  tick.bid_volume[0] = md->BidVolume1;
  tick.bid_volume[1] = md->BidVolume2;
  tick.bid_volume[2] = md->BidVolume3;
  tick.bid_volume[3] = md->BidVolume4;
  tick.bid_volume[4] = md->BidVolume5;

  spdlog::trace(
      "[CtpQuoteApi::OnRtnDepthMarketData] Ticker: {}, Time MS: {}, "
      "LastPrice: {:.2f}, Volume: {}, Turnover: {}, Open Interest: {}",
      contract->ticker, tick.time_ms, tick.last_price, tick.volume,
      tick.turnover, tick.open_interest);

  engine_->on_tick(&tick);
}

void CtpQuoteApi::OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *for_quote_rsp) {}

}  // namespace ft
