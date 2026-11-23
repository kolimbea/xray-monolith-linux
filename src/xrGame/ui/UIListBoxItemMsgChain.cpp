#include "StdAfx.h" // xrGame precompiled header (Windows found it case-insensitively)
#include "UIListBoxItemMsgChain.h"
#include "UIScrollView.h"

bool CUIListBoxItemMsgChain::OnMouseDown(int mouse_btn)
{
	inherited::OnMouseDown(mouse_btn);
	return false;
}
