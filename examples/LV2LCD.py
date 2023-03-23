import espidf as esp
import lvgl as lv
import lv_utils
import lcd
from machine import Pin
import micropython

COLOR_MODE_RGB = const(0x00)
COLOR_MODE_BGR = const(0x08)

micropython.alloc_emergency_exception_buf(256)
# leave enough room for SPI master TX DMA buffers

class LV2LCD:
    display_name = 'lv2lcd'

    # Default values of "power" and "backlight" are reversed logic! 0 means ON.
    # You can change this by setting backlight_on and power_on arguments.

    def __init__(self,
        d0=18, d1=7, d2=8, d3=15, d4=3, d5=16, d6=9, d7=17, cd=13, wt=11, rd=14, _cs=10,
        mhz=2 * 1000 * 1000, _width=320, _height=240, factor=8, swap_color=False, _cmd_bits=8, _param_bits=8,
        double_buffer=True, asynchronous=False, color_format=COLOR_MODE_RGB
    ):

        # Initializations
        
        if not lv.is_initialized():
            lv.init()
        
        self.width = _width
        self.height =  _height
        self.i8080 = lcd.I80(data=(Pin(d0), Pin(d1), Pin(d2), Pin(d3), Pin(d4), Pin(d5), Pin(d6), Pin(d7)),
                     command=Pin(cd),
                     write=Pin(wt),
                     read=Pin(rd),
                     cs=Pin(_cs),
                     pclk=mhz,
                     width=self.width,
                     height=self.height,
                     swap_color_bytes=swap_color,
                     cmd_bits=_cmd_bits,
                     param_bits=_param_bits)
        self.st = lcd.ST7789(self.i8080, reset=Pin(12), backlight=Pin(6))
        self.st.backlight_on()
        self.st.reset()
        self.st.init()
        self.st.invert_color(False)
        self.st.swap_xy(True)
        self.st.mirror(False, True)
        self.st.set_gap(0, 0)
        self.start_x = 0
        self.start_y = 0
        
        self.asynchronous = asynchronous
        self.factor = factor
        self.buf_size = (self.width * self.height * lv.color_t.__SIZE__) // factor
        # Register display driver

        self.buf1 = esp.heap_caps_malloc(self.buf_size, esp.MALLOC_CAP.DMA)
        self.buf2 = esp.heap_caps_malloc(self.buf_size, esp.MALLOC_CAP.DMA) if double_buffer else None
        
        if self.buf1 and self.buf2:
            print("Double buffer")
        elif self.buf1:
            print("Single buffer")
        else:
            raise RuntimeError("Not enough DMA-able memory to allocate display buffer")
        
        self.disp_buf = lv.disp_draw_buf_t()
        self.disp_drv = lv.disp_drv_t()
        
        self.disp_buf.init(self.buf1, self.buf2, self.buf_size // lv.color_t.__SIZE__)
        self.disp_drv.init()

        self.disp_drv.user_data = {
            'bus_obj': self.i8080,
            'start_x': self.start_x,
            'start_y': self.start_y}
        self.disp_drv.draw_buf = self.disp_buf
        self.disp_drv.flush_cb = esp.lcd_bus_flush
#         self.disp_drv.flush_cb = self.st.lv_flush_test
#         self.disp_drv.flush_cb = self.flush
        self.disp_drv.monitor_cb = self.monitor
        self.disp_drv.hor_res = self.width
        self.disp_drv.ver_res = self.height
        if color_format:
            self.disp_drv.color_format = color_format
        print('yes1')
        self.disp_drv.register()
        print('yes2')
        if not lv_utils.event_loop.is_running():
            self.event_loop = lv_utils.event_loop(asynchronous=self.asynchronous)
    monitor_acc_time = 0
    monitor_acc_px = 0
    monitor_count = 0

    cycles_in_ms = esp.esp_clk_cpu_freq() // 1000
    
    def monitor(self, disp_drv, time, px):
        self.monitor_acc_time += time
        self.monitor_acc_px += px
        self.monitor_count += 1
    
    def flush(self, disp_drv, area, color_p):
        fbuf = framebuf.FrameBuffer(bytearray(320 * 480 * 2), 320, 480, framebuf.RGB565)
        fbuf.fill(25525)
        self.st.bitmap(0, 0, 320, 240, fbuf)
        print('flush')
    
    def deinit(self):

        print('Deinitializing {}..'.format(self.display_name))
        # Prevent callbacks to lvgl, which refer to the buffers we are about to delete
        if lv_utils.event_loop.is_running():
            self.event_loop.deinit()  
        if self.st:
            self.st.deinit()
        if self.i8080:
            self.i8080.deinit()
        self.disp.remove()
        # Free RAM
        if self.buf1:
            esp.heap_caps_free(self.buf1)
            self.buf1 = None
        if self.buf2:
            esp.heap_caps_free(self.buf2)
            self.buf2 = None
            
# 2. 封装的需要显示的按钮       
class CounterBtn():
    def __init__(self, scr):
        self.cnt = 0
        btn = lv.btn(scr)  # 将当前按钮与screen对象进行关联
        # btn.set_pos(20, 10)  # 相对于屏幕左上角 x为20，y为10
        btn.set_size(120, 50)  # 设置按钮的宽度为120, 高度为50
        btn.align(lv.ALIGN.CENTER,0,0)  # 居中（第1个0表示x的偏移量，第2个0表示相对于y的偏移量）
        btn.add_event_cb(self.btn_event_cb, lv.EVENT.ALL, None)  # 设置按钮被按下后的回调函数
        label = lv.label(btn)  # 在按钮上创建一个标签Label，用来显示文字用
        label.set_text("Button")  # 设置文字内容
        label.center()  # 相对于父对象居中

    def btn_event_cb(self, evt):
        code = evt.get_code()  # 获取点击事件类型码
        btn = evt.get_target()  # 获取被点击的对象，此时就是按钮
        if code == lv.EVENT.CLICKED:
            self.cnt += 1

        # Get the first child of the button which is the label and change its text
        label = btn.get_child(0)
        label.set_text("Button: " + str(self.cnt))  # 修改文字内容
    ######################################################

if __name__ == '__main__':
    lv.init()
    lv2lcd = LV2LCD()
    lv.obj()
    # 1. 创建显示screen对象。将需要显示的组件添加到这个screen才能显示
    scr = lv.obj()
    # 2. 创建按钮
    counterBtn = CounterBtn(scr)
    # 3. 显示screen对象中的内容
    lv.scr_load(scr)


