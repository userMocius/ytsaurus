#pragma once

#include "public.h"
#include <ytlib/yson/public.h>

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

void ThrowInvalidNodeType(IConstNodePtr node, ENodeType expectedType, ENodeType actualType);
void ThrowNoSuchChildKey(IConstNodePtr node, const Stroka& key);
void ThrowNoSuchChildIndex(IConstNodePtr node, int index);
void ThrowNoSuchAttribute(const Stroka& key);
void ThrowNoSuchSystemAttribute(const Stroka& key);
void ThrowNoSuchUserAttribute(const Stroka& key);
void ThrowVerbNotSuppored(const Stroka& verb);
void ThrowVerbNotSuppored(const Stroka& verb, const Stroka& resolveType);
void ThrowVerbNotSuppored(IConstNodePtr node, const Stroka& verb);
void ThrowCannotHaveChildren(IConstNodePtr node);
void ThrowAlreadyExists(IConstNodePtr node);
void ThrowCannotRemoveAttribute(const Stroka& key);
void ThrowCannotSetSystemAttribute(const Stroka& key);
void ThrowCannotSetOpaqueAttribute(const Stroka& key);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
