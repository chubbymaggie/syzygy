// Copyright 2014 Google Inc. All Rights Reserved.
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
//
// This file declares implementation classes to generate assembly code.
// The API to the assembler is intentionally very close to the API exposed
// by the V8 assembler (see src/ia32/assembler-ia32.* in V8 repository).

#ifndef SYZYGY_ASSM_ASSEMBLER_BASE_H_
#define SYZYGY_ASSM_ASSEMBLER_BASE_H_

#include "syzygy/assm/cond.h"
#include "syzygy/assm/label_base.h"
#include "syzygy/assm/operand_base.h"
#include "syzygy/assm/register.h"
#include "syzygy/assm/value_base.h"

namespace assm {

// The reference sizes the assembler supports coincides with register sizes.
typedef RegisterSize ReferenceSize;

// The assembler takes care of maintaining an output location (address), and
// generating a stream of bytes and references as instructions are assembled.
template <class ReferenceType>
class AssemblerBase {
 public:
  typedef DisplacementBase<ReferenceType> Displacement;
  typedef ImmediateBase<ReferenceType> Immediate;
  typedef OperandBase<ReferenceType> Operand;
  typedef LabelBase<ReferenceType> Label;

  // The maximum size of a single NOP instruction. Any NOPs generated beyond
  // this length will actually consist of multiple consecutive NOP
  // instructions.
  static const size_t kMaxNopInstructionSize = 11;

  // Tracks a single embedded reference in the instruction.
  struct ReferenceInfo {
    size_t offset;
    ReferenceType reference;
    ReferenceSize size;
    bool pc_relative;
  };

  // The assembler pushes instructions and references to
  // one of these for serialization.
  class InstructionSerializer {
   public:
    virtual void AppendInstruction(uint32_t location,
                                   const uint8_t* bytes,
                                   uint32_t num_bytes,
                                   const ReferenceInfo* refs,
                                   size_t num_refs) = 0;
    virtual bool FinalizeLabel(uint32_t location,
                               const uint8_t* bytes,
                               size_t num_bytes) = 0;
  };

  // Constructs an assembler that assembles to @p delegate
  // starting at @p location.
  AssemblerBase(uint32_t location, InstructionSerializer* serializer);

  // @name Accessors.
  // @{
  uint32_t location() const { return location_; }
  void set_location(uint32_t location) { location_ = location; }
  // @}

  // Emits one or more NOP instructions, their total length being @p size
  // bytes.
  // @param size The number of bytes of NOPs to generate.
  // @note For a generated NOP sequence of optimal performance it is best to
  //     call nop once rather than successively (ie: the NOP sequence generated
  //     by nop(x) nop(y) may perform worse than that generated by nop(x + y).
  void nop(size_t size);

  // @name Call instructions.
  // @{
  void call(const Immediate& dst);
  void call(const Operand& dst);
  // @}

 protected:
  // @name Control flow instructions.
  // These instructions are protected, as they're not appropriate to expose
  // for all assembler subclasses.
  // @{
  void j(ConditionCode cc, const Immediate& dst);

  // @param cc the condition code to generate.
  // @param dst the label to jump to.
  // @param size the requested size/reach of the instruction. Will generate the
  //     optimal reach if kSizeNone and the label is bound. Will generate long
  //     reach if kSizeNone and the label is unbound.
  // @returns true if successful, false if the requested reach is
  //     inappropriate.
  bool j(ConditionCode cc, Label* dst, RegisterSize size);
  bool j(ConditionCode cc, Label* dst);
  void jecxz(const Immediate& dst);
  void jmp(const Immediate& dst);
  void jmp(const Operand& dst);
  void jmp(const Register32& dst);
  void l(LoopCode lc, const Immediate& dst);

 public:
  void ret();
  void ret(uint16_t n);
  // @}

  // @name Set flags.
  // @{
  void set(ConditionCode cc, const Register32& src);
  // @}

  // @name Byte mov varieties.
  // @{
  void mov_b(const Operand& dst, const Immediate& src);
  void movzx_b(const Register32& dst, const Operand& src);
  // @}

  // @name Double-word mov varieties.
  // @{
  void mov(const Register32& dst, const Register32& src);
  void mov(const Register32& dst, const Operand& src);
  void mov(const Operand& dst, const Register32& src);
  void mov(const Register32& dst, const Immediate& src);
  void mov(const Operand& dst, const Immediate& src);
  void mov_fs(const Register32& dst, const Operand& src);
  void mov_fs(const Operand& dst, const Register32& src);
  // @}

  // @name Load effective address.
  void lea(const Register32& dst, const Operand& src);

  // @name Stack manipulation.
  // @{
  void push(const Register32& src);
  void push(const Immediate& src);
  void push(const Operand& src);
  void pushad();

  void pop(const Register32& dst);
  void pop(const Operand& dst);
  void popad();
  // @}

  // @name Flag manipulation.
  // @{
  void pushfd();
  void popfd();
  void lahf();
  void sahf();
  // @}

  // @name Arithmetic operations.
  // @{
  void test(const Register8& dst, const Register8& src);
  void test(const Register8& dst, const Immediate& src);

  void test(const Register32& dst, const Register32& src);
  void test(const Register32& dst, const Operand& src);
  void test(const Operand& dst, const Register32& src);
  void test(const Register32& dst, const Immediate& src);
  void test(const Operand& dst, const Immediate& src);

  void cmp(const Register8& dst, const Register8& src);
  void cmp(const Register8& dst, const Immediate& src);

  void cmp(const Register32& dst, const Register32& src);
  void cmp(const Register32& dst, const Operand& src);
  void cmp(const Operand& dst, const Register32& src);
  void cmp(const Register32& dst, const Immediate& src);
  void cmp(const Operand& dst, const Immediate& src);

  void add(const Register8& dst, const Register8& src);
  void add(const Register8& dst, const Immediate& src);

  void add(const Register32& dst, const Register32& src);
  void add(const Register32& dst, const Operand& src);
  void add(const Operand& dst, const Register32& src);
  void add(const Register32& dst, const Immediate& src);
  void add(const Operand& dst, const Immediate& src);

  void sub(const Register8& dst, const Register8& src);
  void sub(const Register8& dst, const Immediate& src);

  void sub(const Register32& dst, const Register32& src);
  void sub(const Register32& dst, const Operand& src);
  void sub(const Operand& dst, const Register32& src);
  void sub(const Register32& dst, const Immediate& src);
  void sub(const Operand& dst, const Immediate& src);

  void imul(const Register32& dst, const Register32& src);
  void imul(const Register32& dst, const Operand& src);
  void imul(const Register32& dst, const Register32& base,
            const Immediate& index);
  // @}

  // @name Logical operations.
  // @{
  void and(const Register8& dst, const Register8& src);
  void and(const Register8& dst, const Immediate& src);

  void and(const Register32& dst, const Register32& src);
  void and(const Register32& dst, const Operand& src);
  void and(const Operand& dst, const Register32& src);
  void and(const Register32& dst, const Immediate& src);
  void and(const Operand& dst, const Immediate& src);

  void xor(const Register8& dst, const Register8& src);
  void xor(const Register8& dst, const Immediate& src);

  void xor(const Register32& dst, const Register32& src);
  void xor(const Register32& dst, const Operand& src);
  void xor(const Operand& dst, const Register32& src);
  void xor(const Register32& dst, const Immediate& src);
  void xor(const Operand& dst, const Immediate& src);
  // @}

  // @name Shifting operations.
  // @{
  void shl(const Register32& dst, const Immediate& src);
  void shr(const Register32& dst, const Immediate& src);
  // @}

  // Exchange contents of two registers.
  // @param dst The destination register.
  // @param src The source register.
  // @note Exchanges involving eax generate shorter byte code.
  void xchg(const Register32& dst, const Register32& src);
  void xchg(const Register16& dst, const Register16& src);
  void xchg(const Register8& dst, const Register8& src);

  // Exchange contents of a register and memory.
  // @param dst The destination register.
  // @param src The source memory location.
  // @note This instruction can be used as a primitive for writing
  //     synchronization mechanisms as there is an implicit lock taken
  //     on @p src during execution.
  void xchg(const Register32& dst, const Operand& src);

  // @name Aliases
  // @{
  void loop(const Immediate& dst) { l(kLoopOnCounter, dst); }
  void loope(const Immediate& dst) { l(kLoopOnCounterAndZeroFlag, dst); }
  void loopne(const Immediate& dst) {
    l(kLoopOnCounterAndNotZeroFlag, dst);
  }
  // @}

  // Insert a single data byte, not an instruction.
  // @param b The value of the byte to insert.
  void data(uint8_t b);

 private:
  friend class Label;
  class InstructionBuffer;

  // @name Nop instruction helpers.
  // @{
  // Each of these corresponds to a basic suggested NOP sequence. They
  // can each be extended by prefixing with 1 or more operand size (0x66)
  // prefixes. These are not exposed directly as the user should simply
  // call 'nop' instead.
  // @param prefix_count The number of operand size prefix bytes to apply.
  void nop1(size_t prefix_count);
  void nop4(size_t prefix_count);
  void nop5(size_t prefix_count);
  void nop7(size_t prefix_count);
  void nop8(size_t prefix_count);
  // @}

  // Output the instruction data in @p instr to our delegate.
  void Output(const InstructionBuffer& instr);

  // Finalizes the use of an unbound label.
  bool FinalizeLabel(uint32_t location,
                     uint32_t destination,
                     RegisterSize size);

  // Stores the current location of assembly.
  uint32_t location_;

  // The delegate we push instructions at.
  InstructionSerializer* serializer_;
};

}  // namespace assm

#include "syzygy/assm/assembler_base_impl.h"

#endif  // SYZYGY_ASSM_ASSEMBLER_BASE_H_
