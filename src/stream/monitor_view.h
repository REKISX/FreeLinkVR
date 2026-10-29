#pragma once
#include "common/graphics.h"
#include "common/image_dump.h"
#include <dcomp.h>
#include <d3d11_1.h>
#include <functional>

namespace slvr {
// The excluded local preview must not fully occlude the stereo swap chain:
// DWM may stop presenting that window, leaving no desktop-capture image.
// OBS has a separate window sharing the mono composition surface.
class MonitorView {
    HWND window_=nullptr,obsWindow_=nullptr,toolbar_=nullptr,owner_=nullptr;
    POINT origin_{};
    Graphics& gpu_;
    ComPtr<IDCompositionDevice> device_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<IDCompositionTarget> obsTarget_;
    ComPtr<IDCompositionVisual> obsVisual_;
    ComPtr<IDCompositionSurface> surface_;
    ComPtr<ID3D11DeviceContext1> context_;
    UINT width_=0,height_=0;
    static LRESULT CALLBACK Proc(HWND w,UINT msg,WPARAM a,LPARAM b){
        if(msg==WM_NCHITTEST)return HTTRANSPARENT;
        if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(msg==WM_CLOSE){PostMessageW(reinterpret_cast<HWND>(GetWindowLongPtrW(w,GWLP_USERDATA)),WM_KEYDOWN,VK_F8,0);return 0;}
        return DefWindowProcW(w,msg,a,b);
    }
    static LRESULT CALLBACK ToolbarProc(HWND w,UINT msg,WPARAM a,LPARAM b){
        if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(msg==WM_LBUTTONUP){PostMessageW(reinterpret_cast<HWND>(GetWindowLongPtrW(w,GWLP_USERDATA)),WM_SYSCOMMAND,SC_MINIMIZE,0);return 0;}
        if(msg==WM_PAINT){
            PAINTSTRUCT paint{};HDC dc=BeginPaint(w,&paint);RECT rect{};GetClientRect(w,&rect);HBRUSH brush=CreateSolidBrush(RGB(32,49,45));FillRect(dc,&rect,brush);DeleteObject(brush);
            HFONT font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");const auto old=SelectObject(dc,font);
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,247,242));DrawTextW(dc,L"—  Minimize",-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);DeleteObject(font);EndPaint(w,&paint);return 0;
        }
        return DefWindowProcW(w,msg,a,b);
    }
public:
    explicit MonitorView(Graphics& gpu):gpu_(gpu){}
    ~MonitorView(){Hide();surface_.Reset();obsVisual_.Reset();visual_.Reset();obsTarget_.Reset();target_.Reset();device_.Reset();if(toolbar_)DestroyWindow(toolbar_);if(obsWindow_)DestroyWindow(obsWindow_);if(window_)DestroyWindow(window_);}
    void Open(HWND owner){
        owner_=owner;
        WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"FreeLinkVRMonitor";
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register monitor preview");
        window_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"FreeLinkVR - Monitor",WS_POPUP,0,0,1280,720,owner,nullptr,cls.hInstance,nullptr);
        if(!window_)throw std::runtime_error("Cannot create monitor preview");
        SetWindowLongPtrW(window_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(owner));
        if(!SetWindowDisplayAffinity(window_,WDA_EXCLUDEFROMCAPTURE))throw std::runtime_error("Cannot protect stereo desktop capture");
        cls.lpszClassName=L"FreeLinkVROBS";
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register OBS output");
        obsWindow_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_APPWINDOW|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"FreeLinkVR - OBS (Mono 16:9)",WS_POPUP,0,0,1280,720,nullptr,nullptr,cls.hInstance,nullptr);
        if(!obsWindow_)throw std::runtime_error("Cannot create OBS output");
        SetWindowLongPtrW(obsWindow_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(owner));
        if(!SetWindowDisplayAffinity(obsWindow_,WDA_NONE))throw std::runtime_error("Cannot enable OBS window capture");
        cls.lpszClassName=L"FreeLinkVRControls";cls.lpfnWndProc=ToolbarProc;cls.hCursor=LoadCursorW(nullptr,IDC_HAND);
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register preview controls");
        toolbar_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Minimize FreeLinkVR",WS_POPUP,0,0,150,40,owner,nullptr,cls.hInstance,nullptr);
        if(!toolbar_)throw std::runtime_error("Cannot create minimize control");SetWindowLongPtrW(toolbar_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(owner));
        if(!SetWindowDisplayAffinity(toolbar_,WDA_EXCLUDEFROMCAPTURE))throw std::runtime_error("Cannot exclude local controls from capture");
        ComPtr<IDXGIDevice> dxgi;Check(gpu_.device.As(&dxgi),"Query preview DXGI device");
        Check(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&device_)),"Create local compositor");
        Check(gpu_.context.As(&context_),"Query preview D3D11.1 context");
        Check(device_->CreateTargetForHwnd(window_,TRUE,&target_),"Create local composition target");
        Check(device_->CreateVisual(&visual_),"Create local visual");
        Check(target_->SetRoot(visual_.Get()),"Attach local visual");
        Check(device_->CreateTargetForHwnd(obsWindow_,TRUE,&obsTarget_),"Create OBS composition target");
        Check(device_->CreateVisual(&obsVisual_),"Create OBS visual");
        Check(obsTarget_->SetRoot(obsVisual_.Get()),"Attach OBS visual");
    }
    void Hide(){HideLocal();if(obsWindow_)ShowWindow(obsWindow_,SW_HIDE);}
    void HideLocal(){if(window_)ShowWindow(window_,SW_HIDE);if(toolbar_)ShowWindow(toolbar_,SW_HIDE);}
    HWND Window()const{return obsWindow_;}
    void Draw(bool show,const std::function<void(ID3D11RenderTargetView*,UINT,UINT,POINT)>& render,const std::filesystem::path& snapshot={},bool localVisible=true){
        const bool minimized=IsIconic(owner_)!=FALSE||!localVisible;
        RECT rect{};
        if(minimized){rect.right=width_?static_cast<LONG>(width_):1280;rect.bottom=height_?static_cast<LONG>(height_):720;}
        else{GetClientRect(owner_,&rect);origin_={};ClientToScreen(owner_,&origin_);}
        const POINT origin=origin_;
        const UINT width=std::max<LONG>(1,rect.right),height=std::max<LONG>(1,rect.bottom);
        if(!surface_||width!=width_||height!=height_){
            surface_.Reset();Check(device_->CreateSurface(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&surface_),"Create monitor surface");
            Check(visual_->SetContent(surface_.Get()),"Attach monitor surface");width_=width;height_=height;
            Check(obsVisual_->SetContent(surface_.Get()),"Share monitor surface with OBS");
        }
        ComPtr<IDXGISurface> dxgiSurface;POINT offset{};
        Check(surface_->BeginDraw(nullptr,IID_PPV_ARGS(&dxgiSurface),&offset),"Begin monitor draw");
        try{
            ComPtr<IDXGISurface2> sub;Check(dxgiSurface.As(&sub),"Query monitor surface");
            ComPtr<ID3D11Texture2D> texture;UINT subresource=0;Check(sub->GetResource(IID_PPV_ARGS(&texture),&subresource),"Get monitor texture");
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            if(desc.ArraySize>1){view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.MipSlice=subresource%desc.MipLevels;view.Texture2DArray.FirstArraySlice=subresource/desc.MipLevels;view.Texture2DArray.ArraySize=1;}
            else{view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;view.Texture2D.MipSlice=subresource;}
            ComPtr<ID3D11RenderTargetView> rtv;Check(gpu_.device->CreateRenderTargetView(texture.Get(),&view,&rtv),"Create monitor render target");
            const float black[]{0,0,0,1};const D3D11_RECT update{offset.x,offset.y,offset.x+static_cast<LONG>(width),offset.y+static_cast<LONG>(height)};
            context_->ClearView(rtv.Get(),black,&update,1);render(rtv.Get(),width,height,offset);
            gpu_.context->OMSetRenderTargets(0,nullptr,nullptr);
            if(!snapshot.empty()){
                D3D11_TEXTURE2D_DESC read{};read.Width=width;read.Height=height;read.ArraySize=read.MipLevels=1;read.SampleDesc.Count=1;read.Format=DXGI_FORMAT_B8G8R8A8_UNORM;read.Usage=D3D11_USAGE_STAGING;read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging;Check(gpu_.device->CreateTexture2D(&read,nullptr,&staging),"Create monitor readback");
                const D3D11_BOX area{static_cast<UINT>(offset.x),static_cast<UINT>(offset.y),0,static_cast<UINT>(offset.x)+width,static_cast<UINT>(offset.y)+height,1};
                gpu_.context->CopySubresourceRegion(staging.Get(),0,0,0,0,texture.Get(),subresource,&area);
                D3D11_MAPPED_SUBRESOURCE pixels{};Check(gpu_.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels),"Read monitor frame");
                std::vector<unsigned char> bgra(static_cast<size_t>(width)*height*4);
                for(UINT y=0;y<height;++y)memcpy(bgra.data()+static_cast<size_t>(y)*width*4,static_cast<const unsigned char*>(pixels.pData)+y*pixels.RowPitch,static_cast<size_t>(width)*4);
                gpu_.context->Unmap(staging.Get(),0);WriteBitmap(snapshot,width,height,bgra);
                // Test the actual GPU result: the local test grid must contain
                // only the blue left eye, while the stereo test retains both.
                const auto center=(static_cast<size_t>(height/2)*width+width/2)*4;
                if(bgra[center]<=bgra[center+2])throw std::runtime_error("Monitor preview did not isolate the left eye");
                const auto margin=(static_cast<size_t>(height/2)*width+1)*4;
                if(bgra[margin]||bgra[margin+1]||bgra[margin+2])throw std::runtime_error("Local monitor frame was cropped instead of fitted with margins");
            }
            Check(surface_->EndDraw(),"End monitor draw");
        }catch(...){surface_->EndDraw();throw;}
        Check(device_->Commit(),"Commit local preview");
        if(show){
            // WGC captures this window while occluded. Keeping it at the bottom
            // prevents desktop-mode Steam Link from ever seeing the OBS eye.
            SetWindowPos(obsWindow_,HWND_BOTTOM,origin.x,origin.y,static_cast<int>(width),static_cast<int>(height),SWP_NOACTIVATE|SWP_SHOWWINDOW);
            if(!minimized){
                // Leave two rows of the stereo client uncovered. The preview
                // stays excluded from desktop capture, while DWM keeps the
                // underlying source eligible for presentation after restore.
                SetWindowPos(window_,HWND_TOPMOST,origin.x,origin.y,static_cast<int>(width),std::max(1,static_cast<int>(height)-2),SWP_NOACTIVATE|SWP_SHOWWINDOW);
                SetWindowPos(toolbar_,HWND_TOPMOST,origin.x+static_cast<int>(width)-166,origin.y+16,150,40,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            }else{HideLocal();}
        }
        else Hide();
    }
};
}
