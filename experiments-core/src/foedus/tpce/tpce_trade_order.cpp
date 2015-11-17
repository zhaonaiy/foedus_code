/*
 * Copyright (c) 2014-2015, Hewlett-Packard Development Company, LP.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details. You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * HP designates this particular file as subject to the "Classpath" exception
 * as provided by HP in the LICENSE.txt file that accompanied this code.
 */
#include "foedus/tpce/tpce_client.hpp"

#include <glog/logging.h>

#include "foedus/engine.hpp"
#include "foedus/storage/storage.hpp"
#include "foedus/storage/storage_manager.hpp"

namespace foedus {
namespace tpce {

ErrorCode TpceClientTask::do_trade_order() {
  auto trades = storages_.trades_;
  auto trades_index = storages_.trades_secondary_symb_dts_;
  auto trade_types = storages_.trade_types_;
  // We omit Frame-1 and -2.

  // Frame-3
  // This is also drastically simplified from the full spec.
  // Our intent here is to focus on the behavior around
  // TRADE/TRADE_TYPE. We aren't even trying the full spec.
  const char* in_trade_type_id;
  uint32_t r = rnd_.next_uint32() % TradeTypeData::kCount;
  switch (r) {
    case TradeTypeData::kTlb:
      in_trade_type_id = "TLB";
      break;
    case TradeTypeData::kTls:
      in_trade_type_id = "TLS";
      break;
    case TradeTypeData::kTmb:
      in_trade_type_id = "TMB";
      break;
    case TradeTypeData::kTms:
      in_trade_type_id = "TMS";
      break;
    default:
      in_trade_type_id = "TSL";
  }

  // Lookup in TRADE_TYPE. It's just 5 records. Better to just scan all.
  TradeTypeData tt_record;
  bool type_found = false;
  for (uint32_t i = 0; i < TradeTypeData::kCount; ++i) {
    CHECK_ERROR_CODE(trade_types.get_record(context_, i, &tt_record));
    if (std::memcmp(tt_record.id_, in_trade_type_id, sizeof(tt_record.id_)) == 0) {
      type_found = true;
      break;
    }
  }
  ASSERT_ND(type_found);

  // Frame-4
  // This is about the same as full spec except that
  // the inputs are directly coming from above code
  // or fixed numbers.
  const Datetime now_dts = get_articifical_current_dts();
  const TradeT tid = get_artificial_new_trade_id();
  DVLOG(3) << "tid=" << tid << ", now_dts=" << now_dts;
  TradeData record;
  record.dts_ = now_dts;
  record.id_ = tid;
  // TODO(Hideaki) Set other fields. do the same as data loading...

  CHECK_ERROR_CODE(trades.insert_record<TradeT>(context_, tid, &record, sizeof(record)));

  SymbDtsKey secondary_key = to_symb_dts_key(record.symb_id_, now_dts, worker_id_);
  CHECK_ERROR_CODE(trades_index.insert_record_normalized(
    context_,
    secondary_key,
    &tid,
    sizeof(tid)));
  return kErrorCodeOk;
}

}  // namespace tpce
}  // namespace foedus
