/******************************************************************************
*
*
* metapath - The universal Explorer-like Plugin
*
* DropSource.h
*   OLE drop source functionality
*
* See Readme.txt for more information about this source code.
* Please send me your comments to this work.
*
* See License.txt for details about distribution and modification.
*
*                                              (c) Florian Balmer 1996-2011
*                                                  florian.balmer@gmail.com
*                                              https://www.flos-freeware.ch
*
*
******************************************************************************/
#pragma once

class CDropSource final : public IDropSource {
public:

	/* IUnknown methods */
	STDMETHODIMP QueryInterface(REFIID riid, PVOID *ppv) noexcept override;
	STDMETHODIMP_(ULONG)AddRef() noexcept override;
	STDMETHODIMP_(ULONG)Release() noexcept override;

	/* IDropSource methods */
	STDMETHODIMP QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) noexcept override;
	STDMETHODIMP GiveFeedback(DWORD /*dwEffect*/) noexcept override;

private:
	ULONG m_refs = 1;
};
