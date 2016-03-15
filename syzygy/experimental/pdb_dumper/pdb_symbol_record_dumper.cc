// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "syzygy/pdb/pdb_symbol_record.h"

#include <string>

#include "base/strings/stringprintf.h"
#include "syzygy/common/align.h"
#include "syzygy/experimental/pdb_dumper/pdb_dump_util.h"
#include "syzygy/experimental/pdb_dumper/pdb_leaf.h"
#include "syzygy/pdb/pdb_reader.h"
#include "syzygy/pdb/pdb_util.h"
#include "syzygy/pe/cvinfo_ext.h"

namespace pdb {

namespace cci = Microsoft_Cci_Pdb;

namespace {

template <typename SymbolType>
bool ReadSymbolAndName(PdbStream* stream,
                       uint16_t len,
                       SymbolType* symbol_out,
                       std::string* name_out) {
  DCHECK(stream != NULL);
  DCHECK(len > 0);
  DCHECK(symbol_out != NULL);
  DCHECK(name_out != NULL);

  // Note the zero-terminated name field must be the trailing field
  // of the symbol.
  size_t to_read = offsetof(SymbolType, name);
  size_t bytes_read = 0;
  if (!stream->ReadBytes(symbol_out, to_read, &bytes_read) ||
      !ReadString(stream, name_out) ||
      bytes_read != to_read) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }

  return true;
}

// Return the string value associated with a symbol type.
const char* SymbolTypeName(uint16_t symbol_type) {
  switch (symbol_type) {
// Just return the name of the enum.
#define SYM_TYPE_NAME(sym_type, unused) \
    case cci::sym_type: { \
      return #sym_type; \
    }
    SYM_TYPE_CASE_TABLE(SYM_TYPE_NAME)
#undef SYM_TYPE_NAME
    default :
      return NULL;
  }
}

void DumpLvarAddrRange(FILE* out,
                       uint8_t indent_level,
                       const CvLvarAddrRange& range) {
  DumpIndentedText(out, indent_level, "Range:\n");
  DumpIndentedText(out, indent_level + 1, "offStart: 0x%08X\n", range.offStart);
  DumpIndentedText(out, indent_level + 1, "isectStart: %d\n", range.isectStart);
  DumpIndentedText(out, indent_level + 1, "cbRange: 0x%04X\n", range.cbRange);
}

bool DumpLvarAddrGaps(FILE* out,
                      PdbStream* stream,
                      uint16_t len,
                      uint8_t indent_level) {
  DumpIndentedText(out, indent_level, "Gaps:\n");
  uint16_t bytes_left = len;
  CvLvarAddrGap gap = {};
  size_t to_read = sizeof(CvLvarAddrGap);
  while (bytes_left >= to_read) {
    size_t bytes_read = 0;
    if (!stream->ReadBytes(&gap, to_read, &bytes_read) ||
        bytes_read != to_read) {
      LOG(ERROR) << "Unable to read symbol record.";
      return false;
    }
    bytes_left -= bytes_read;  // Note: bytes_left >= to_read = bytes_read
    DumpIndentedText(out, indent_level + 1, "gapStartOffset: 0x%04X\n",
                     gap.gapStartOffset);
    DumpIndentedText(out, indent_level + 1, "cbRange: 0x%04X\n", gap.cbRange);
  }

  // Note: alignment is 4, same as sizeof(CvLvarAddrGap).
  if (bytes_left > 0) {
    LOG(ERROR) << "Unexpected symbol record length.";
    return false;
  }

  return true;
}

// Dump a symbol record using RefSym2 struct to out.
bool DumpRefSym2(FILE* out,
                 PdbStream* stream,
                 uint16_t len,
                 uint8_t indent_level) {
  cci::RefSym2 symbol_info = {};
  std::string symbol_name;
  if (!ReadSymbolAndName(stream, len, &symbol_info, &symbol_name))
    return false;

  DumpIndentedText(out, indent_level, "Name: %s\n", symbol_name.c_str());
  DumpIndentedText(out, indent_level, "SUC: %d\n", symbol_info.sumName);
  DumpIndentedText(out, indent_level, "Offset: 0x%08X\n", symbol_info.ibSym);
  DumpIndentedText(out, indent_level, "Module: %d\n", symbol_info.imod);

  return true;
}

// Dump a symbol record using DatasSym32 struct to out.
bool DumpDatasSym32(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  cci::DatasSym32 symbol_info = {};
  std::string symbol_name;
  if (!ReadSymbolAndName(stream, len, &symbol_info, &symbol_name))
    return false;

  DumpIndentedText(out, indent_level, "Name: %s\n", symbol_name.c_str());
  DumpIndentedText(out, indent_level, "Type index: 0x%08X\n",
                   symbol_info.typind);
  DumpIndentedText(out, indent_level, "Offset: 0x%08X\n", symbol_info.off);
  DumpIndentedText(out, indent_level, "Segment: 0x%04X\n", symbol_info.seg);
  return true;
}

bool DumpPubSym32(FILE* out,
                  PdbStream* stream,
                  uint16_t len,
                  uint8_t indent_level) {
  cci::PubSym32 symbol_info = {};
  std::string symbol_name;
  if (!ReadSymbolAndName(stream, len, &symbol_info, &symbol_name))
    return false;

  DumpIndentedText(out, indent_level, "Name: %s\n", symbol_name.c_str());
  DumpIndentedText(out, indent_level, "Flags:\n");
  DumpIndentedText(out, indent_level + 1, "fCode: %d\n",
                   (symbol_info.flags & cci::fCode) != 0);
  DumpIndentedText(out, indent_level + 1, "fFunction: %d\n",
                   (symbol_info.flags & cci::fFunction) != 0);
  DumpIndentedText(out, indent_level + 1, "fManaged: %d\n",
                   (symbol_info.flags & cci::fManaged) != 0);
  DumpIndentedText(out, indent_level + 1, "fMSIL: %d\n",
                   (symbol_info.flags & cci::fMSIL) != 0);

  DumpIndentedText(out, indent_level, "Offset: 0x%08X\n", symbol_info.off);
  DumpIndentedText(out, indent_level, "Segment: 0x%04X\n", symbol_info.seg);
  return true;
}

bool DumpOemSymbol(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpVpathSym32(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpFrameProcSym(FILE* out,
                      PdbStream* stream,
                      uint16_t len,
                      uint8_t indent_level) {
  cci::FrameProcSym frame_proc_sym = {};
  if (!stream->Read(&frame_proc_sym, 1))
    return false;

  DumpIndentedText(out, indent_level, "cbFrame: 0x%08X\n",
                   frame_proc_sym.cbFrame);
  DumpIndentedText(out, indent_level, "cbPad: 0x%08X\n", frame_proc_sym.cbPad);
  DumpIndentedText(out, indent_level, "offPad: 0x%08X\n",
                   frame_proc_sym.offPad);
  DumpIndentedText(out, indent_level, "cbSaveRegs: 0x%08X\n",
                   frame_proc_sym.cbSaveRegs);
  DumpIndentedText(out, indent_level, "offExHdlr: 0x%08X\n",
                   frame_proc_sym.offExHdlr);
  DumpIndentedText(out, indent_level, "secExHdlr: %d\n",
                   frame_proc_sym.secExHdlr);

  FrameProcSymFlags convert = {frame_proc_sym.flags};
  DumpIndentedText(out, indent_level, "Flags:\n");
  DumpIndentedText(out, indent_level + 1, "HasAlloca              : %d\n",
                   convert.fHasAlloca);
  DumpIndentedText(out, indent_level + 1, "HasSetJmp              : %d\n",
                   convert.fHasSetJmp);
  DumpIndentedText(out, indent_level + 1, "HasLongJmp             : %d\n",
                   convert.fHasLongJmp);
  DumpIndentedText(out, indent_level + 1, "HasInlAsm              : %d\n",
                   convert.fHasInlAsm);
  DumpIndentedText(out, indent_level + 1, "HasEH                  : %d\n",
                   convert.fHasEH);
  DumpIndentedText(out, indent_level + 1, "InlSpec                : %d\n",
                   convert.fInlSpec);
  DumpIndentedText(out, indent_level + 1, "HasSEH                 : %d\n",
                   convert.fHasSEH);
  DumpIndentedText(out, indent_level + 1, "Naked                  : %d\n",
                   convert.fNaked);
  DumpIndentedText(out, indent_level + 1, "SecurityChecks         : %d\n",
                   convert.fSecurityChecks);
  DumpIndentedText(out, indent_level + 1, "AsyncEH                : %d\n",
                   convert.fAsyncEH);
  DumpIndentedText(out, indent_level + 1, "GSNoStackOrdering      : %d\n",
                   convert.fGSNoStackOrdering);
  DumpIndentedText(out, indent_level + 1, "WasInlined             : %d\n",
                   convert.fWasInlined);
  DumpIndentedText(out, indent_level + 1, "Reserved               : %d\n",
                   convert.reserved);

  return true;
}

bool DumpAnnotationSym(FILE* out,
                       PdbStream* stream,
                       uint16_t len,
                       uint8_t indent_level) {
  cci::AnnotationSym symbol_info = {};

  size_t to_read = offsetof(cci::AnnotationSym, rgsz);
  size_t bytes_read = 0;
  if (!stream->ReadBytes(&symbol_info, to_read, &bytes_read) ||
      bytes_read != to_read) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }

  DumpIndentedText(out, indent_level, "Offset: 0x%08X\n", symbol_info.off);
  DumpIndentedText(out, indent_level, "Segment: 0x%04X\n", symbol_info.seg);
  DumpIndentedText(out, indent_level, "Number of strings: %d\n",
      symbol_info.csz);

  for (int i = 0; i < symbol_info.csz; ++i) {
    std::string annotation;
    if (!ReadString(stream, &annotation)) {
      LOG(ERROR) << "Unable to read an annotation.";
      return false;
    }
    DumpIndentedText(out, indent_level + 1, "%d: %s\n", i, annotation.c_str());
  }

  return true;
}

bool DumpManyTypRef(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpObjNameSym(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  DCHECK_NE(reinterpret_cast<FILE*>(NULL), out);
  DCHECK_NE(reinterpret_cast<PdbStream*>(NULL), stream);
  cci::ObjNameSym sym = {};
  std::string name;
  if (!ReadSymbolAndName(stream, len, &sym, &name))
    return false;
  DumpIndentedText(out, indent_level, "Signature: 0x%08X\n", sym.signature);
  DumpIndentedText(out, indent_level, "Name     : %s\n", name.c_str());
  return true;
}

bool DumpThunkSym32(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpBlockSym32(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpWithSym32(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpLabelSym32(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpRegSym(FILE* out,
                PdbStream* stream,
                uint16_t len,
                uint8_t indent_level) {
  DCHECK_NE(reinterpret_cast<FILE*>(nullptr), out);
  DCHECK_NE(reinterpret_cast<PdbStream*>(nullptr), stream);
  cci::RegSym sym = {};
  std::string name;
  if (!ReadSymbolAndName(stream, len, &sym, &name))
    return false;
  DumpIndentedText(out, indent_level, "Type index: 0x%08X\n", sym.typind);
  DumpIndentedText(out, indent_level, "Register: %d\n", sym.reg);
  DumpIndentedText(out, indent_level, "Name     : %s\n", name.c_str());
  return true;
}

bool DumpConstSym(FILE* out,
                  PdbStream* stream,
                  uint16_t len,
                  uint8_t indent_level) {
  size_t to_read = offsetof(cci::ConstSym, name);
  size_t bytes_read = 0;
  cci::ConstSym symbol_info = {};
  if (!stream->ReadBytes(&symbol_info, to_read, &bytes_read) ||
      bytes_read != to_read) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }

  // If the value field is less than LF_NUMERIC then the value data is the value
  // of that symbol. Otherwise it refers to the value data and the actual value
  // is located after this field.
  size_t name_start_offset = 0;
  const char* value_type = GetNumericLeafNameAndSize(symbol_info.value,
                                                     &name_start_offset);

  if (value_type == NULL) {
    DumpIndentedText(out, indent_level, "Value: 0x%04X\n", symbol_info.value);
  } else {
    DumpIndentedText(out, indent_level, "Value: type=%s, value=", value_type);
    DumpNumericLeaf(out, symbol_info.value, stream);
    ::fprintf(out, "\n");
  }
  std::string symbol_name;
  if (!ReadString(stream, &symbol_name)) {
    LOG(ERROR) << "Unable to read the name of a symbol record.";
    return false;
  }
  DumpIndentedText(out, indent_level, "Name: %s\n", symbol_name.c_str());
  DumpIndentedText(out, indent_level, "Type index: 0x%08X\n",
      symbol_info.typind);

  return true;
}

bool DumpUdtSym(FILE* out,
                PdbStream* stream,
                uint16_t len,
                uint8_t indent_level) {
  cci::UdtSym symbol_info = {};
  std::string symbol_name;
  if (!ReadSymbolAndName(stream, len, &symbol_info, &symbol_name))
    return false;

  DumpIndentedText(out, indent_level, "Name: %s\n", symbol_name.c_str());
  DumpIndentedText(out, indent_level, "Type index: 0x%08X\n",
                   symbol_info.typind);
  return true;
}

bool DumpManyRegSym(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpBpRelSym32(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  cci::BpRelSym32 bp_rel_sym = {};
  std::string name;

  if (!ReadSymbolAndName(stream, len, &bp_rel_sym, &name))
    return false;

  DumpIndentedText(out, indent_level, "off: %d\n", bp_rel_sym.off);
  DumpIndentedText(out, indent_level, "typind: 0x%08X\n", bp_rel_sym.typind);
  DumpIndentedText(out, indent_level, "Name: %s\n", name.c_str());

  return true;
}

bool DumpProcSym32(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  DCHECK_NE(reinterpret_cast<FILE*>(NULL), out);
  DCHECK_NE(reinterpret_cast<PdbStream*>(NULL), stream);
  cci::ProcSym32 sym = {};
  std::string name;
  if (!ReadSymbolAndName(stream, len, &sym, &name))
    return false;
  DumpIndentedText(out, indent_level, "Parent     : 0x%08X\n", sym.parent);
  DumpIndentedText(out, indent_level, "End        : 0x%08X\n", sym.end);
  DumpIndentedText(out, indent_level, "Next       : 0x%08X\n", sym.next);
  DumpIndentedText(out, indent_level, "Length     : 0x%08X\n", sym.len);
  DumpIndentedText(out, indent_level, "Debug start: 0x%08X\n", sym.dbgStart);
  DumpIndentedText(out, indent_level, "Debug end  : 0x%08X\n", sym.dbgEnd);
  DumpIndentedText(out, indent_level, "Type index : 0x%08X\n", sym.typind);
  DumpIndentedText(out, indent_level, "Offset     : 0x%08X\n", sym.off);
  DumpIndentedText(out, indent_level, "Segment    : %d\n", sym.seg);
  DumpIndentedText(out, indent_level, "Flags:\n");
  DumpIndentedText(out, indent_level + 1, "No FPO              : %d\n",
                   (sym.flags & cci::CV_PFLAG_NOFPO) > 0);
  DumpIndentedText(out, indent_level + 1, "Interrupt return    : %d\n",
                   (sym.flags & cci::CV_PFLAG_INT) > 0);
  DumpIndentedText(out, indent_level + 1, "Far return          : %d\n",
                   (sym.flags & cci::CV_PFLAG_FAR) > 0);
  DumpIndentedText(out, indent_level + 1, "No return           : %d\n",
                   (sym.flags & cci::CV_PFLAG_NEVER) > 0);
  DumpIndentedText(out, indent_level + 1, "Not reached         : %d\n",
                   (sym.flags & cci::CV_PFLAG_NOTREACHED) > 0);
  DumpIndentedText(out, indent_level + 1, "Custom call         : %d\n",
                   (sym.flags & cci::CV_PFLAG_CUST_CALL) > 0);
  DumpIndentedText(out, indent_level + 1, "No inline           : %d\n",
                   (sym.flags & cci::CV_PFLAG_NOINLINE) > 0);
  DumpIndentedText(out, indent_level + 1, "Optimized debug info: %d\n",
                   (sym.flags & cci::CV_PFLAG_OPTDBGINFO) > 0);
  DumpIndentedText(out, indent_level, "Name: %s\n", name.c_str());
  return true;
}

bool DumpRegRel32(FILE* out,
                  PdbStream* stream,
                  uint16_t len,
                  uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpThreadSym32(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  cci::ThreadSym32 symbol_info = {};
  std::string symbol_name;
  if (!ReadSymbolAndName(stream, len, &symbol_info, &symbol_name))
    return false;

  DumpIndentedText(out, indent_level, "Name: %s\n", symbol_name.c_str());
  DumpIndentedText(out, indent_level, "Offset: %d\n", symbol_info.off);
  DumpIndentedText(out, indent_level, "Segment: %d\n", symbol_info.seg);
  DumpIndentedText(out, indent_level, "Type index: 0x%08X\n",
                   symbol_info.typind);
  return true;
}

bool DumpProcSymMips(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpCompileSymFlags(FILE* out,
                         const CompileSymFlags& flags,
                         uint8_t indent_level) {
  DCHECK_NE(reinterpret_cast<FILE*>(NULL), out);
  DumpIndentedText(out, indent_level, "Flags:\n");
  DumpIndentedText(out, indent_level + 1, "Language         : %d\n",
                   flags.iLanguage);
  DumpIndentedText(out, indent_level + 1, "Edit and continue: %d\n",
                   flags.fEC);
  DumpIndentedText(out, indent_level + 1, "No debug info    : %d\n",
                   flags.fNoDbgInfo);
  DumpIndentedText(out, indent_level + 1, "LTCG             : %d\n",
                   flags.fLTCG);
  DumpIndentedText(out, indent_level + 1, "No data align    : %d\n",
                   flags.fNoDataAlign);
  DumpIndentedText(out, indent_level + 1, "Managed present  : %d\n",
                   flags.fManagedPresent);
  DumpIndentedText(out, indent_level + 1, "Security checks  : %d\n",
                   flags.fSecurityChecks);
  DumpIndentedText(out, indent_level + 1, "Hot patch        : %d\n",
                   flags.fHotPatch);
  return true;
}

// Dumps a CompileSym or a CompileSym2. Care must be taken to ensure that
// |CompileSymType| and |symbol_version| agree.
template <typename CompileSymType>
bool DumpCompileSymImpl(FILE* out,
                        PdbStream* stream,
                        uint16_t len,
                        uint8_t indent_level,
                        int symbol_version) {
  DCHECK_NE(reinterpret_cast<FILE*>(NULL), out);
  DCHECK_NE(reinterpret_cast<PdbStream*>(NULL), stream);

  std::vector<char> data(len, 0);
  size_t bytes_read = 0;
  if (!stream->ReadBytes(data.data(), len, &bytes_read) ||
      bytes_read != len) {
    return false;
  }

  // Dump the flags. These are the same in both structures.
  const CompileSymFlags* cf =
      reinterpret_cast<const CompileSymFlags*>(data.data());
  if (!DumpCompileSymFlags(out, *cf, indent_level))
    return false;

  // Dump the rest of the fields.
  const CompileSymType* cs =
      reinterpret_cast<const CompileSymType*>(data.data());
  const CompileSym2* cs2 =
      reinterpret_cast<const CompileSym2*>(data.data());
  DumpIndentedText(out, indent_level, "Machine                : %d\n",
                   cs->machine);
  DumpIndentedText(out, indent_level, "Front-end major version: %d\n",
                   cs->verFEMajor);
  DumpIndentedText(out, indent_level, "Front-end minor version: %d\n",
                   cs->verFEMinor);
  DumpIndentedText(out, indent_level, "Front-end build number : %d\n",
                   cs->verFEBuild);
  if (symbol_version == 2) {
    DumpIndentedText(out, indent_level, "Front-end revision     : %d\n",
                     cs2->verFERevision);
  }
  DumpIndentedText(out, indent_level, "Back-end major version : %d\n",
                   cs->verMajor);
  DumpIndentedText(out, indent_level, "Back-end minor version : %d\n",
                   cs->verMinor);
  DumpIndentedText(out, indent_level, "Back-end build number  : %d\n",
                   cs->verBuild);
  if (symbol_version == 2) {
    DumpIndentedText(out, indent_level, "Back-end revision      : %d\n",
                     cs2->verRevision);
  }

  size_t version_string = offsetof(CompileSymType, verSt);

  // Dump the compiler version string.
  const char* str = data.data() + version_string;
  size_t max_len = data.size() - version_string;
  size_t str_len = ::strnlen(str, max_len);
  DumpIndentedText(out, indent_level, "Version string         : %.*s\n",
                   str_len, str);
  str += str_len + 1;
  max_len -= str_len + 1;

  // Dump any arguments.
  if (max_len > 0 && str[0] != 0) {
    DumpIndentedText(out, indent_level, "Version string arguments:\n");
    size_t i = 0;
    while (str[0] != NULL) {
      str_len = ::strnlen(str, max_len);
      DumpIndentedText(out, indent_level + 1, "%d: %.*s\n", i, str);
      ++i;
      str += str_len + 1;
      max_len -= str_len + 1;
    }
  }

  return true;
}

bool DumpCompileSym(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  DCHECK_NE(reinterpret_cast<FILE*>(NULL), out);
  DCHECK_NE(reinterpret_cast<PdbStream*>(NULL), stream);
  if (!DumpCompileSymImpl<cci::CompileSym>(out, stream, len, indent_level, 1))
    return false;
  return true;
}

bool DumpCompileSym2(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  DCHECK_NE(reinterpret_cast<FILE*>(NULL), out);
  DCHECK_NE(reinterpret_cast<PdbStream*>(NULL), stream);
  if (!DumpCompileSymImpl<CompileSym2>(out, stream, len, indent_level, 2))
    return false;
  return true;
}

bool DumpManyRegSym2(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpProcSymIa64(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpSlotSym32(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpFrameRelSym(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpAttrRegSym(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpAttrSlotSym(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpAttrManyRegSym(FILE* out,
                        PdbStream* stream,
                        uint16_t len,
                        uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpAttrRegRel(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpAttrManyRegSym2(FILE* out,
                         PdbStream* stream,
                         uint16_t len,
                         uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpUnamespaceSym(FILE* out,
                       PdbStream* stream,
                       uint16_t len,
                       uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpManProcSym(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpTrampolineSym(FILE* out,
                       PdbStream* stream,
                       uint16_t len,
                       uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpSepCodSym(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  cci::SepCodSym symbol_info = {};
  if (!stream->Read(&symbol_info, 1))
    return false;

  DumpIndentedText(out, indent_level, "parent: %d\n", symbol_info.parent);
  DumpIndentedText(out, indent_level, "end: %d\n", symbol_info.end);
  DumpIndentedText(out, indent_level, "length: %d\n", symbol_info.length);
  DumpIndentedText(out, indent_level, "scf: %d\n", symbol_info.scf);
  DumpIndentedText(out, indent_level, "off: %d\n", symbol_info.off);
  DumpIndentedText(out, indent_level, "offParent: %d\n", symbol_info.offParent);
  DumpIndentedText(out, indent_level, "sec: %d\n", symbol_info.sec);
  DumpIndentedText(out, indent_level, "secParent: %d\n", symbol_info.secParent);

  return true;
}

bool DumpLocalSym(FILE* out,
                  PdbStream* stream,
                  uint16_t len,
                  uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpLocalSym2013(FILE* out,
                      PdbStream* stream,
                      uint16_t len,
                      uint8_t indent_level) {
  LocalSym2013 symbol_info = {};
  std::string symbol_name;
  if (!ReadSymbolAndName(stream, len, &symbol_info, &symbol_name)) {
    return false;
  }

  DCHECK_NE(reinterpret_cast<FILE*>(NULL), out);
  DumpIndentedText(out, indent_level, "typeind: 0x%08X\n", symbol_info.typind);
  DumpIndentedText(out, indent_level, "Flags:\n");
  DumpIndentedText(out, indent_level + 1, "IsParam            : %d\n",
                   symbol_info.flags.fIsParam);
  DumpIndentedText(out, indent_level + 1, "AddrTaken          : %d\n",
                   symbol_info.flags.fAddrTaken);
  DumpIndentedText(out, indent_level + 1, "CompGenx           : %d\n",
                   symbol_info.flags.fCompGenx);
  DumpIndentedText(out, indent_level + 1, "IsAggregate        : %d\n",
                   symbol_info.flags.fIsAggregate);
  DumpIndentedText(out, indent_level + 1, "IsAggregated       : %d\n",
                   symbol_info.flags.fIsAggregated);
  DumpIndentedText(out, indent_level + 1, "IsAliased          : %d\n",
                   symbol_info.flags.fIsAliased);
  DumpIndentedText(out, indent_level + 1, "IsAlias            : %d\n",
                   symbol_info.flags.fIsAlias);
  DumpIndentedText(out, indent_level + 1, "fIsRetValue        : %d\n",
    symbol_info.flags.fIsRetValue);
  DumpIndentedText(out, indent_level + 1, "fIsOptimizedOut    : %d\n",
                   symbol_info.flags.fIsOptimizedOut);
  DumpIndentedText(out, indent_level + 1, "fIsEnregGlob       : %d\n",
                   symbol_info.flags.fIsEnregGlob);
  DumpIndentedText(out, indent_level + 1, "fIsEnregStat       : %d\n",
                   symbol_info.flags.fIsEnregStat);
  DumpIndentedText(out, indent_level + 1, "reserved           : %d\n",
                   symbol_info.flags.reserved);
  DumpIndentedText(out, indent_level, "name: %s\n", symbol_name.c_str());

  return true;
}

bool DumpDefrangeSymRegister(FILE* out,
                             PdbStream* stream,
                             uint16_t len,
                             uint8_t indent_level) {
  // TODO(manzagop): this would be easier (and safer) if we passed in a shallow
  // stream of shorter length.
  uint16_t bytes_left = len;

  // Read the fixed part.
  size_t to_read = offsetof(DefrangeSymRegister, gaps);
  size_t bytes_read = 0;
  DefrangeSymRegister sym;
  if (to_read > len || !stream->ReadBytes(&sym, to_read, &bytes_read) ||
      bytes_read != to_read) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }
  bytes_left -= bytes_read;

  DumpIndentedText(out, indent_level, "Register: %d\n", sym.reg);
  DumpIndentedText(out, indent_level, "attr.maybe: %d\n", sym.attr.maybe);
  DumpLvarAddrRange(out, indent_level, sym.range);

  // Read and dump the variable length part.
  return DumpLvarAddrGaps(out, stream, bytes_left, indent_level);
}

bool DumpDefRangeSymFramePointerRel(FILE* out,
                                    PdbStream* stream,
                                    uint16_t len,
                                    uint8_t indent_level) {
  // TODO(manzagop): this would be easier (and safer) if we passed in a shallow
  // stream of shorter length.
  uint16_t bytes_left = len;

  // Read the fixed part.
  size_t to_read = offsetof(DefRangeSymFramePointerRel, gaps);
  size_t bytes_read = 0;
  DefRangeSymFramePointerRel sym;
  if (to_read > len || !stream->ReadBytes(&sym, to_read, &bytes_read) ||
      bytes_read != to_read) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }
  bytes_left -= bytes_read;

  DumpIndentedText(out, indent_level + 1, "offFramePointer: %d\n",
                   sym.offFramePointer);
  DumpLvarAddrRange(out, indent_level, sym.range);

  // Read and dump the variable length part.
  return DumpLvarAddrGaps(out, stream, bytes_left, indent_level);
}

bool DumpDefRangeSymSubfieldRegister(FILE* out,
                                     PdbStream* stream,
                                     uint16_t len,
                                     uint8_t indent_level) {
  // TODO(manzagop): this would be easier (and safer) if we passed in a shallow
  // stream of shorter length.
  uint16_t bytes_left = len;

  // Read the fixed part.
  size_t to_read = offsetof(DefRangeSymSubfieldRegister, gaps);
  size_t bytes_read = 0;
  DefRangeSymSubfieldRegister sym;
  if (to_read > len || !stream->ReadBytes(&sym, to_read, &bytes_read) ||
      bytes_read != to_read) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }
  bytes_left -= bytes_read;

  DumpIndentedText(out, indent_level, "Register: %d\n", sym.reg);
  DumpIndentedText(out, indent_level, "attr.maybe: %d\n", sym.attr.maybe);
  DumpIndentedText(out, indent_level, "offParent: 0x%04X\n", sym.offParent);
  DumpLvarAddrRange(out, indent_level, sym.range);

  // Read and dump the variable length part.
  return DumpLvarAddrGaps(out, stream, bytes_left, indent_level);
}

bool DumpFPOffs2013(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  FPOffs2013 fp_offs = {};
  if (!stream->Read(&fp_offs, 1))
    return false;

  DumpIndentedText(out, indent_level, "Offs: %d\n", fp_offs.offs);
  return true;
}

bool DumpDefRangeSymRegisterRel(FILE* out,
                                PdbStream* stream,
                                uint16_t len,
                                uint8_t indent_level) {
  // TODO(manzagop): this would be easier (and safer) if we passed in a shallow
  // stream of shorter length.
  uint16_t bytes_left = len;

  // Read the fixed part.
  size_t to_read = offsetof(DefRangeSymRegisterRel, gaps);
  size_t bytes_read = 0;
  DefRangeSymRegisterRel sym;
  if (to_read > len || !stream->ReadBytes(&sym, to_read, &bytes_read) ||
      bytes_read != to_read) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }
  bytes_left -= bytes_read;

  DumpIndentedText(out, indent_level, "baseReg: %d\n", sym.baseReg);
  DumpIndentedText(out, indent_level, "spilledUdtMember: %d\n",
                   sym.spilledUdtMember);
  DumpIndentedText(out, indent_level, "offsetParent: 0x%04X\n",
                   sym.offsetParent);
  DumpIndentedText(out, indent_level, "offBasePointer: %d\n",
                   sym.offBasePointer);
  DumpLvarAddrRange(out, indent_level, sym.range);

  // Read and dump the variable length part.
  return DumpLvarAddrGaps(out, stream, bytes_left, indent_level);
}

bool DumpInlineSiteSym(FILE* out,
                       PdbStream* stream,
                       uint16_t len,
                       uint8_t indent_level) {
  size_t bytes_left = len;

  // Read the fixed part.
  size_t to_read = offsetof(InlineSiteSym, binaryAnnotations);
  size_t bytes_read = 0;
  InlineSiteSym sym;
  if (!stream->ReadBytes(&sym, to_read, &bytes_read) || bytes_read != to_read) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }
  bytes_left -= bytes_read;

  DumpIndentedText(out, indent_level, "pParent: 0x%08X\n", sym.pParent);
  DumpIndentedText(out, indent_level, "pEnd: 0x%08X\n", sym.pEnd);
  DumpIndentedText(out, indent_level, "inlinee: 0x%08X\n", sym.inlinee);

  DumpIndentedText(out, indent_level, "binaryAnnotations:\n");
  return DumpUnknownBlock(out, stream, bytes_left, indent_level + 1);
}

bool DumpDefRangeSym(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpDefRangeSym2(FILE* out,
                      PdbStream* stream,
                      uint16_t len,
                      uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpSectionSym(FILE* out,
                    PdbStream* stream,
                    uint16_t len,
                    uint8_t indent_level) {
  cci::SectionSym section = {};
  std::string section_name;
  if (!ReadSymbolAndName(stream, len, &section, &section_name))
    return false;

  DumpIndentedText(out, indent_level, "isec: %d\n", section.isec);
  DumpIndentedText(out, indent_level, "align: %d\n", section.align);
  DumpIndentedText(out, indent_level, "bReserved: %d\n", section.bReserved);
  DumpIndentedText(out, indent_level, "rva: 0x%08X\n", section.rva);
  DumpIndentedText(out, indent_level, "cb: %d\n", section.cb);
  DumpIndentedText(out, indent_level, "characteristics: 0x%08X\n",
                   section.characteristics);
  DumpIndentedText(out, indent_level, "name: %s\n", section_name.c_str());

  return true;
}

bool DumpCoffGroupSym(FILE* out,
                      PdbStream* stream,
                      uint16_t len,
                      uint8_t indent_level) {
  cci::CoffGroupSym coff_group = {};
  std::string coff_group_name;
  if (!ReadSymbolAndName(stream, len, &coff_group, &coff_group_name))
    return false;

  DumpIndentedText(out, indent_level, "cb: %d\n", coff_group.cb);
  DumpIndentedText(out, indent_level, "characteristics: 0x%08X\n",
                   coff_group.characteristics);
  DumpIndentedText(out, indent_level, "off: %d\n", coff_group.off);
  DumpIndentedText(out, indent_level, "seg: %d\n", coff_group.seg);
  DumpIndentedText(out, indent_level, "name: %s\n", coff_group_name.c_str());

  return true;
}

bool DumpExportSym(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  cci::ExportSym export_sym;
  std::string name;

  if (!ReadSymbolAndName(stream, offsetof(cci::ExportSym, name), &export_sym,
                         &name))
    return false;

  ExportVarFlags convert = {export_sym.flags};
  DumpIndentedText(out, indent_level, "Ordinal: %d\n", export_sym.ordinal);
  DumpIndentedText(out, indent_level, "Flags:\n");
  DumpIndentedText(out, indent_level + 1, "Constant            : %d\n",
                   convert.fConstant);
  DumpIndentedText(out, indent_level + 1, "Data                : %d\n",
                   convert.fData);
  DumpIndentedText(out, indent_level + 1, "Private             : %d\n",
                   convert.fPrivate);
  DumpIndentedText(out, indent_level + 1, "NoName              : %d\n",
                   convert.fNoName);
  DumpIndentedText(out, indent_level + 1, "Ordinal             : %d\n",
                   convert.fOrdinal);
  DumpIndentedText(out, indent_level + 1, "Forwarder           : %d\n",
                   convert.fForwarder);
  DumpIndentedText(out, indent_level + 1, "Reserved            : %d\n",
                   convert.reserved);
  DumpIndentedText(out, indent_level, "name: %s\n", name.c_str());

  return true;
}

bool DumpCallsiteInfo(FILE* out,
                      PdbStream* stream,
                      uint16_t len,
                      uint8_t indent_level) {
  cci::CallsiteInfo symbol_info = {};
  if (!stream->Read(&symbol_info, 1)) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }
  DumpIndentedText(out, indent_level, "Offset: 0x%08X\n", symbol_info.off);
  DumpIndentedText(out, indent_level, "Section index: 0x%04X\n",
      symbol_info.ect);
  DumpIndentedText(out, indent_level,
      "Type index describing function signature: 0x%08X\n", symbol_info.typind);
  return true;
}

bool DumpFrameCookie(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  cci::FrameCookie frame_cookie = {};
  if (!stream->Read(&frame_cookie, 1))
    return false;

  DumpIndentedText(out, indent_level, "Offs: %d\n", frame_cookie.off);
  DumpIndentedText(out, indent_level, "Reg: %d\n", frame_cookie.reg);
  DumpIndentedText(out, indent_level, "Cookietype: 0x%08X\n",
                   frame_cookie.cookietype);
  DumpIndentedText(out, indent_level, "Flags: 0x%02X\n", frame_cookie.flags);

  return true;
}

bool DumpFrameCookieSym(FILE* out,
                        PdbStream* stream,
                        uint16_t len,
                        uint8_t indent_level) {
  FrameCookieSym frame_cookie = {};
  if (!stream->Read(&frame_cookie, 1))
    return false;

  DumpIndentedText(out, indent_level, "Offs: %d\n", frame_cookie.off);
  DumpIndentedText(out, indent_level, "Reg: %d\n", frame_cookie.reg);
  DumpIndentedText(out, indent_level, "Cookietype: 0x%08X\n",
                   frame_cookie.cookietype);

  return true;
}

bool DumpDiscardedSym(FILE* out,
                      PdbStream* stream,
                      uint16_t len,
                      uint8_t indent_level) {
  // TODO(sebmarchand): Implement this function if we encounter this symbol.
  return false;
}

bool DumpMSToolEnvV3(FILE* out,
                     PdbStream* stream,
                     uint16_t len,
                     uint8_t indent_level) {
  MSToolEnvV3 environment;

  // Read the structure header.
  size_t to_read = offsetof(MSToolEnvV3, key_values);
  size_t bytes_read = 0;
  if (!stream->ReadBytes(&environment, to_read, &bytes_read) ||
      bytes_read != to_read ||
      environment.leading_zero != 0) {
    LOG(ERROR) << "Unable to read symbol record.";
    return false;
  }

  DumpIndentedText(out, indent_level, "Tool Environment (V3):\n");

  // Read an array of key-value pairs of string until key is empty.
  // The remaining padding must be ignored.
  std::string key;
  std::string value;
  while (true) {
    if (!ReadString(stream, &key)) {
      LOG(ERROR) << "Invalid MS Tool format.";
      return false;
    }

    if (key.empty())
      return true;

    if (!ReadString(stream, &value)) {
      LOG(ERROR) << "Invalid MS Tool format.";
      return false;
    }

    DumpIndentedText(out, indent_level + 1, "%s: %s\n",
                     key.c_str(), value.c_str());
  }
}

// Hexdump the data of the undeciphered symbol records.
bool DumpUnknown(FILE* out,
                 PdbStream* stream,
                 uint16_t len,
                 uint8_t indent_level) {
  if (len == 0)
    return true;
  DumpIndentedText(out, indent_level, "Unsupported symbol type.\n");
  DumpIndentedText(out, indent_level + 1, "Length: %d\n", len);
  DumpIndentedText(out, indent_level + 1, "Data:\n");
  return DumpUnknownBlock(out, stream, len, indent_level + 2);
}

// TODO(chrisha|sebmarchand): Implement these! These are simple stubs so that
//     this compiles cleanly.
bool DumpCompileSymCV2(FILE* out,
                       PdbStream* stream,
                       uint16_t len,
                       uint8_t indent_level) {
  return DumpUnknown(out, stream, len, indent_level);
}

bool DumpSearchSym(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  return DumpUnknown(out, stream, len, indent_level);
}

bool DumpEndArgSym(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  return DumpUnknown(out, stream, len, indent_level);
}

bool DumpReturnSym(FILE* out,
                   PdbStream* stream,
                   uint16_t len,
                   uint8_t indent_level) {
  return DumpUnknown(out, stream, len, indent_level);
}

bool DumpEntryThisSym(FILE* out,
                      PdbStream* stream,
                      uint16_t len,
                      uint8_t indent_level) {
  return DumpUnknown(out, stream, len, indent_level);
}

}  //  namespace

void DumpSymbolRecords(FILE* out,
                       PdbStream* stream,
                       const SymbolRecordVector& sym_record_vector,
                       uint8_t indent_level) {
  DCHECK(stream != NULL);

  SymbolRecordVector::const_iterator symbol_iter =
    sym_record_vector.begin();
  // Dump each symbol contained in the vector.
  for (; symbol_iter != sym_record_vector.end(); ++symbol_iter) {
    if (!stream->Seek(symbol_iter->start_position)) {
      LOG(ERROR) << "Unable to seek to symbol record at position "
                 << base::StringPrintf("0x%08X.", symbol_iter->start_position);
      return;
    }
    const char* symbol_type_text = SymbolTypeName(symbol_iter->type);
    if (symbol_type_text != NULL) {
      DumpIndentedText(out,
                       indent_level,
                       "Symbol Type: 0x%04X %s (offset 0x%08X)\n",
                       symbol_iter->type,
                       symbol_type_text,
                       symbol_iter->start_position - 4);
    } else {
      DumpIndentedText(out,
                       indent_level,
                       "Unknown symbol Type: 0x%04X (offset 0x%08X)\n",
                       symbol_iter->type,
                       symbol_iter->start_position - 4);
    }
    bool success = false;
    switch (symbol_iter->type) {
// Call a function to dump a specific (struct_type) kind of structure.
#define SYM_TYPE_DUMP(sym_type, struct_type) \
    case cci::sym_type: { \
      success = Dump ## struct_type(out, \
                                    stream, \
                                    symbol_iter->len, \
                                    indent_level + 1); \
      break; \
    }
      SYM_TYPE_CASE_TABLE(SYM_TYPE_DUMP)
#undef SYM_TYPE_DUMP
    }

    if (!success) {
      // In case of failure we just dump the hex data of this symbol.
      if (!stream->Seek(symbol_iter->start_position)) {
        LOG(ERROR) << "Unable to seek to symbol record at position "
                   << base::StringPrintf("0x%08X.",
                                         symbol_iter->start_position);
        return;
      }
      DumpUnknown(out, stream, symbol_iter->len, indent_level + 1);
    }
    stream->Seek(common::AlignUp(stream->pos(), 4));

    size_t expected_pos = symbol_iter->start_position + symbol_iter->len;
    if (stream->pos() != expected_pos) {
      LOG(ERROR)
          << base::StringPrintf(
                 "Symbol record stream is not valid (after type 0x%04X). ",
                 symbol_iter->type)
          << base::StringPrintf("Position after parsing is %d (expected %d).",
                                stream->pos(), expected_pos);
      return;
    }
  }
}

}  // namespace pdb
