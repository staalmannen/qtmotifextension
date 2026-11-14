/* C++ programs (xpdf) include the Motif headers: they must compile as
 * C++17, where "register" is gone (clang rejects it). Built only. */
#include <Xm/XmAll.h>
#include <Xm/XmP.h>
#include <Xm/PrimitiveP.h>
#include <Xm/ManagerP.h>
#include <Xm/DropSMgrP.h>
int main () { return XmConvertUnits != nullptr ? 0 : 1; }
