#include "notbad.h"

static BOOL ReadAllBytes(LPCWSTR lpFileName, BYTE **ppData, DWORD *pcbData)
{
    HANDLE hFile;
    LARGE_INTEGER size;
    BYTE *pData = NULL;
    DWORD total = 0;

    *ppData = NULL;
    *pcbData = 0;

    hFile = CreateFileW(lpFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!GetFileSizeEx(hFile, &size) || size.QuadPart > MAX_EDIT_BUFFER)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    if (size.QuadPart > 0)
    {
        pData = (BYTE *)LocalAlloc(LMEM_FIXED, (SIZE_T)size.QuadPart + 1);
        if (pData == NULL)
        {
            CloseHandle(hFile);
            return FALSE;
        }

        while (total < (DWORD)size.QuadPart)
        {
            DWORD read = 0;
            if (!ReadFile(hFile, pData + total, (DWORD)size.QuadPart - total, &read, NULL) || read == 0)
                break;
            total += read;
        }

        pData[total] = 0;
    }
    else
    {
        pData = (BYTE *)LocalAlloc(LMEM_ZEROINIT, 1);
        if (pData == NULL)
        {
            CloseHandle(hFile);
            return FALSE;
        }
    }

    CloseHandle(hFile);

    *ppData = pData;
    *pcbData = total;
    return TRUE;
}

static BOOL LooksLikeUtf8(const BYTE *pData, DWORD cbData)
{
    int i;
    BOOL hasHigh = FALSE;

    for (i = 0; i < (int)cbData; ++i)
    {
        if (pData[i] >= 0x80)
        {
            hasHigh = TRUE;
            break;
        }
    }

    if (!hasHigh)
        return FALSE;

    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                               (LPCSTR)pData, (int)cbData, NULL, 0) != 0;
}

BOOL LoadTextFile(LPCWSTR lpFileName, LPWSTR *ppText, TextEncoding *pEncoding)
{
    BYTE *pData = NULL;
    DWORD cbData = 0;
    LPWSTR lpText = NULL;
    TextEncoding encoding = ENC_ANSI;
    UINT codePage = CP_ACP;
    DWORD skip = 0;
    int cchWide = 0;

    *ppText = NULL;
    *pEncoding = ENC_ANSI;

    if (!ReadAllBytes(lpFileName, &pData, &cbData))
        return FALSE;

    if (cbData >= 3 && pData[0] == 0xEF && pData[1] == 0xBB && pData[2] == 0xBF)
    {
        encoding = ENC_UTF8_BOM;
        skip = 3;
    }
    else if (cbData >= 2 && pData[0] == 0xFF && pData[1] == 0xFE)
    {
        encoding = ENC_UTF16LE;
        skip = 2;
    }
    else if (cbData >= 2 && pData[0] == 0xFE && pData[1] == 0xFF)
    {
        encoding = ENC_UTF16BE;
        skip = 2;
    }
    else if (cbData >= 2 && pData[0] != 0 && pData[1] == 0)
    {
        encoding = ENC_UTF16LE;
    }
    else if (cbData >= 2 && pData[0] == 0 && pData[1] != 0)
    {
        encoding = ENC_UTF16BE;
    }
    else if (LooksLikeUtf8(pData, cbData))
    {
        encoding = ENC_UTF8;
    }

    if (encoding == ENC_UTF16LE || encoding == ENC_UTF16BE)
    {
        DWORD cbWide = cbData - skip;
        DWORD i;
        BYTE *pSrc = pData + skip;

        cchWide = (int)(cbWide / 2);
        lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)cchWide + 1) * sizeof(WCHAR));
        if (lpText == NULL)
        {
            LocalFree(pData);
            return FALSE;
        }

        if (encoding == ENC_UTF16LE)
        {
            memcpy(lpText, pSrc, (SIZE_T)cchWide * sizeof(WCHAR));
        }
        else
        {
            for (i = 0; i < (DWORD)cchWide; ++i)
                lpText[i] = (WCHAR)((pSrc[i * 2] << 8) | pSrc[i * 2 + 1]);
        }

        lpText[cchWide] = L'\0';
    }
    else
    {
        if (encoding == ENC_UTF8_BOM)
            codePage = CP_UTF8;
        else if (encoding == ENC_UTF8)
            codePage = CP_UTF8;
        else
            codePage = CP_ACP;

        cchWide = MultiByteToWideChar(codePage, 0, (LPCSTR)(pData + skip),
                                      (int)(cbData - skip), NULL, 0);
        if (cchWide < 0)
            cchWide = 0;

        lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)cchWide + 1) * sizeof(WCHAR));
        if (lpText == NULL)
        {
            LocalFree(pData);
            return FALSE;
        }

        if (cchWide > 0)
        {
            MultiByteToWideChar(codePage, 0, (LPCSTR)(pData + skip),
                                (int)(cbData - skip), lpText, cchWide);
        }

        lpText[cchWide] = L'\0';
    }

    LocalFree(pData);

    *ppText = lpText;
    *pEncoding = encoding;
    return TRUE;
}

BOOL SaveTextFile(LPCWSTR lpFileName, LPCWSTR lpText, TextEncoding encoding)
{
    HANDLE hFile;
    BOOL ok = TRUE;
    int cchText = lstrlenW(lpText);
    BYTE *pBytes = NULL;
    DWORD cbBytes = 0;

    if (encoding == ENC_UTF16LE)
    {
        cbBytes = (DWORD)(cchText * sizeof(WCHAR));
        pBytes = (BYTE *)LocalAlloc(LMEM_FIXED, cbBytes + 2);
        if (pBytes == NULL)
            return FALSE;
        pBytes[0] = 0xFF;
        pBytes[1] = 0xFE;
        memcpy(pBytes + 2, lpText, cbBytes);
        cbBytes += 2;
    }
    else if (encoding == ENC_UTF16BE)
    {
        int i;
        cbBytes = (DWORD)(cchText * sizeof(WCHAR));
        pBytes = (BYTE *)LocalAlloc(LMEM_FIXED, cbBytes + 2);
        if (pBytes == NULL)
            return FALSE;
        pBytes[0] = 0xFE;
        pBytes[1] = 0xFF;
        for (i = 0; i < cchText; ++i)
        {
            pBytes[2 + i * 2] = (BYTE)((lpText[i] >> 8) & 0xFF);
            pBytes[2 + i * 2 + 1] = (BYTE)(lpText[i] & 0xFF);
        }
        cbBytes += 2;
    }
    else
    {
        UINT codePage = CP_ACP;
        DWORD flags = 0;
        int cb;

        if (encoding == ENC_UTF8 || encoding == ENC_UTF8_BOM)
        {
            // Sem WC_ERR_INVALID_CHARS: com a flag, um unico surrogate
            // invalido faz a conversao retornar 0 e o arquivo era gravado
            // vazio. Sem ela, o Windows substitui por U+FFFD.
            codePage = CP_UTF8;
        }
        else if (encoding == ENC_OEM)
        {
            codePage = CP_OEMCP;
        }
        else if (encoding == ENC_GB18030)
        {
            codePage = 54936;
        }

        cb = WideCharToMultiByte(codePage, flags, lpText, cchText, NULL, 0, NULL, NULL);
        if (cb < 0)
            cb = 0;

        pBytes = (BYTE *)LocalAlloc(LMEM_FIXED, (SIZE_T)cb + 4);
        if (pBytes == NULL)
            return FALSE;

        if (encoding == ENC_UTF8_BOM)
        {
            pBytes[0] = 0xEF;
            pBytes[1] = 0xBB;
            pBytes[2] = 0xBF;
            cbBytes = 3;
        }

        if (cb > 0)
        {
            WideCharToMultiByte(codePage, flags, lpText, cchText,
                                (LPSTR)(pBytes + cbBytes), cb, NULL, NULL);
        }

        cbBytes += (DWORD)cb;
    }

    hFile = CreateFileW(lpFileName, GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        LocalFree(pBytes);
        return FALSE;
    }

    if (cbBytes > 0)
    {
        DWORD written = 0;
        if (!WriteFile(hFile, pBytes, cbBytes, &written, NULL) || written != cbBytes)
            ok = FALSE;
    }

    CloseHandle(hFile);
    LocalFree(pBytes);

    return ok;
}
