#pragma once

#include "render/core/texture_handle.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Presentation never changes the editable source or its UTF-8 byte offsets.
// Replaced ranges are atomic editing units; textures are borrowed from the host.
struct TextReplacement {
  std::size_t begin = 0, end = 0;
  std::string text;
  TextureId image;
  int imageWidth = 0, imageHeight = 0;
  bool operator==(const TextReplacement&) const = default;
};

class TextPresentation {
public:
  TextPresentation() = default;
  TextPresentation(std::string_view source, std::vector<TextReplacement> replacements) : m_source(source) {
    std::ranges::stable_sort(replacements, {}, &TextReplacement::begin);
    for (auto& replacement : replacements)
      if (replacement.begin < replacement.end
          && replacement.end <= source.size()
          && (m_replacements.empty() || replacement.begin >= m_replacements.back().end)
          && (static_cast<unsigned char>(source[replacement.begin]) & 0xC0) != 0x80
          && (replacement.end == source.size() || (static_cast<unsigned char>(source[replacement.end]) & 0xC0) != 0x80)
          && !replacement.text.empty())
        m_replacements.push_back(std::move(replacement));
    m_offsets.resize(source.size() + 1);
    std::size_t at = 0;
    const auto literal = [&](std::size_t end) {
      for (; at < end; ++at) {
        m_offsets[at] = m_text.size();
        m_text += source[at];
      }
    };
    for (const auto& replacement : m_replacements) {
      literal(replacement.begin);
      for (; at < replacement.end; ++at)
        m_offsets[at] = m_text.size();
      m_text += replacement.text;
    }
    literal(source.size());
    m_offsets[source.size()] = m_text.size();
  }
  const std::string& text() const { return m_text; }
  const std::vector<TextReplacement>& replacements() const { return m_replacements; }
  std::size_t displayOffset(std::size_t sourceOffset) const {
    return m_offsets.empty() ? 0 : m_offsets[std::min(sourceOffset, m_offsets.size() - 1)];
  }
  bool matches(std::string_view source, const std::vector<TextReplacement>& replacements) const {
    return m_source == source && m_replacements == replacements;
  }
  const TextReplacement* replacementAt(std::size_t offset) const {
    const auto at = std::ranges::upper_bound(m_replacements, offset, {}, &TextReplacement::begin);
    if (at == m_replacements.begin())
      return nullptr;
    const auto& replacement = *std::prev(at);
    return offset < replacement.end ? &replacement : nullptr;
  }

private:
  std::string m_source, m_text;
  std::vector<std::size_t> m_offsets;
  std::vector<TextReplacement> m_replacements;
};
