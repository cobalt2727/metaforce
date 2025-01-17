#pragma once

#include <array>
#include <boo/IWindow.hpp>

namespace metaforce {

struct CKeyboardMouseControllerData {
  std::array<bool, 256> m_charKeys{};
  std::array<bool, static_cast<size_t>(boo::ESpecialKey::MAX)> m_specialKeys{};
  std::array<bool, 6> m_mouseButtons{};
  boo::EModifierKey m_modMask = boo::EModifierKey::None;
  boo::SWindowCoord m_mouseCoord;
  boo::SScrollDelta m_accumScroll;
};

} // namespace metaforce
