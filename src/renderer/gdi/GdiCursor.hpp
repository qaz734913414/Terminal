/*++
Copyright (c) Microsoft Corporation

Module Name:
- GdiCursor.hpp

Abstract:
- Serves as an abstraction of the cursor's drawing responsibilities.
    Different engines will have differing implementations, for ex the 
    GDI engine will need a timer to control blinking the cursor, 

Author(s):
- Mike Griese (migrie) 16-Oct-2017
--*/

#pragma once

#include "..\inc\MinimalCursor.hpp"
#include "..\inc\IRenderEngine.hpp"

namespace Microsoft
{
    namespace Console
    {
        namespace Render
        {
            class GdiCursor;
        }
    }
}

class Microsoft::Console::Render::GdiCursor : public MinimalCursor
{
public:

    GdiCursor(IRenderEngine* const pEngine);

    void Move(_In_ const COORD cPos) override;

private:
    RECT _rcCursorInvert;
    IRenderEngine* const _pEngine;
};