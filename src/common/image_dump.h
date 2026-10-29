#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
namespace slvr {
inline void WriteBitmap(const std::filesystem::path& path,int width,int height,const std::vector<unsigned char>& bgra){
    if(width<=0||height<=0||bgra.size()!=static_cast<size_t>(width)*height*4)throw std::runtime_error("Invalid bitmap");
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+static_cast<DWORD>(bgra.size());
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(&file),sizeof(file));output.write(reinterpret_cast<const char*>(&info),sizeof(info));output.write(reinterpret_cast<const char*>(bgra.data()),static_cast<std::streamsize>(bgra.size()));if(!output)throw std::runtime_error("Cannot write preview");
}
inline void CaptureOwnWindow(HWND window,const std::filesystem::path& path){
    RECT rect{};GetClientRect(window,&rect);const int width=rect.right,height=rect.bottom;
    HDC screen=GetDC(window),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);const auto previous=SelectObject(memory,bitmap);
    PrintWindow(window,memory,PW_CLIENTONLY);SelectObject(memory,previous);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);const int copied=GetDIBits(memory,bitmap,0,height,pixels.data(),&info,DIB_RGB_COLORS);
    DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,screen);if(!copied)throw std::runtime_error("Cannot capture preview");for(size_t i=3;i<pixels.size();i+=4)pixels[i]=255;WriteBitmap(path,width,height,pixels);
}
}
