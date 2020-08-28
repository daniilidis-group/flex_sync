/* -*-c++-*--------------------------------------------------------------------
 * 2020 Bernd Pfrommer bernd.pfrommer@gmail.com
 */
#ifndef FLEX_SYNC_MSG_PACK_H
#define FLEX_SYNC_MSG_PACK_H

/*
 * helper class for type deduction
 */

namespace flex_sync {
  template <typename... Args>
  struct MsgPack {}; // for exposing message types
}
#endif // FLEX_SYNC_MSG_PACK_H
