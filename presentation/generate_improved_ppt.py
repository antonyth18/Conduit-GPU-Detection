from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.enum.text import PP_ALIGN
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE

def set_slide_background(slide, color):
    background = slide.background
    fill = background.fill
    fill.solid()
    fill.fore_color.rgb = color

def create_title_slide(prs):
    blank_slide_layout = prs.slide_layouts[6] 
    slide = prs.slides.add_slide(blank_slide_layout)
    set_slide_background(slide, RGBColor(11, 19, 43)) # Very Dark Blue Background

    # Add Title Box
    txBox = slide.shapes.add_textbox(Inches(0.5), Inches(1.2), Inches(9), Inches(2.5))
    tf = txBox.text_frame
    tf.word_wrap = True
    
    p = tf.paragraphs[0]
    p.text = "GPU-Accelerated Conflict Detection & Dependency Graph Construction"
    p.font.bold = True
    p.font.size = Pt(36)
    p.font.color.rgb = RGBColor(255, 255, 255)
    p.alignment = PP_ALIGN.CENTER

    p2 = tf.add_paragraph()
    p2.text = "for Parallel Smart Contract Execution using CUDA"
    p2.font.bold = False
    p2.font.size = Pt(24)
    p2.font.color.rgb = RGBColor(0, 212, 255) # Cyan Accent
    p2.alignment = PP_ALIGN.CENTER
    
    p2.space_before = Pt(15)

    # Add Team details Box (Placed lower to prevent overlap)
    txBox2 = slide.shapes.add_textbox(Inches(1), Inches(4.0), Inches(8), Inches(1.5))
    tf2 = txBox2.text_frame
    tf2.word_wrap = True
    
    p3 = tf2.paragraphs[0]
    p3.text = "Aditya Raj (241IT006)  |  Antony Thaikadavil (241IT010)\nNikhil Soni (241IT048)  |  Sucheth K Katte (241IT078)"
    p3.font.size = Pt(16)
    p3.font.color.rgb = RGBColor(200, 200, 200)
    p3.alignment = PP_ALIGN.CENTER
    
    p4 = tf2.add_paragraph()
    p4.text = "\nDepartment of Information Technology\nNational Institute of Technology Karnataka, Surathkal"
    p4.font.size = Pt(14)
    p4.font.color.rgb = RGBColor(150, 150, 150)
    p4.alignment = PP_ALIGN.CENTER

def add_content_slide(prs, title, bullets):
    blank_slide_layout = prs.slide_layouts[6]
    slide = prs.slides.add_slide(blank_slide_layout)
    set_slide_background(slide, RGBColor(248, 249, 250)) # Very clean light gray/white
    
    # Add Header Rectangle
    header_shape = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), Inches(10), Inches(1.0))
    header_shape.fill.solid()
    header_shape.fill.fore_color.rgb = RGBColor(28, 37, 65) # Navy Blue
    header_shape.line.fill.background()
    
    # Add Title inside Header
    txBox = slide.shapes.add_textbox(Inches(0.5), Inches(0.15), Inches(9), Inches(0.8))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = title
    p.font.bold = True
    p.font.size = Pt(28)
    p.font.color.rgb = RGBColor(255, 255, 255)
    
    # Add Content Box
    txBox_content = slide.shapes.add_textbox(Inches(0.5), Inches(1.3), Inches(9), Inches(4.0))
    tf_c = txBox_content.text_frame
    tf_c.word_wrap = True
    
    for i, bullet in enumerate(bullets):
        if i == 0:
            p_c = tf_c.paragraphs[0]
        else:
            p_c = tf_c.add_paragraph()
            
        p_c.text = "•  " + bullet
        p_c.font.size = Pt(20)
        p_c.font.color.rgb = RGBColor(40, 40, 40)
        p_c.space_after = Pt(22)
        
def create_thank_you_slide(prs):
    blank_slide_layout = prs.slide_layouts[6] 
    slide = prs.slides.add_slide(blank_slide_layout)
    set_slide_background(slide, RGBColor(11, 19, 43)) # Dark Blue

    txBox = slide.shapes.add_textbox(Inches(2), Inches(2.2), Inches(6), Inches(2))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = "Thank You!"
    p.font.bold = True
    p.font.size = Pt(54)
    p.font.color.rgb = RGBColor(0, 212, 255)
    p.alignment = PP_ALIGN.CENTER
    
    p2 = tf.add_paragraph()
    p2.text = "Questions & Discussion"
    p2.font.size = Pt(24)
    p2.font.color.rgb = RGBColor(255, 255, 255)
    p2.alignment = PP_ALIGN.CENTER
    p2.space_before = Pt(20)

def main():
    prs = Presentation()
    # 16:9 Aspect Ratio for Modern Look
    prs.slide_width = Inches(10)
    prs.slide_height = Inches(5.625)

    # Slide 1: Title
    create_title_slide(prs)

    # Slide 2: Problem & Pipeline
    add_content_slide(prs, "Problem Statement & Proposed Pipeline", [
        "The CPU Bottleneck: Current parallel execution systems (e.g., Block-STM) rely on CPU scheduling, which scales poorly for large blocks (10k-100k transactions).",
        "Our Solution: Offload conflict detection and scheduling to massively parallel GPUs.",
        "Transaction Analysis: Using thousands of CUDA threads to independently extract read/write state access patterns.",
        "Parallel Scheduling: Rapidly identifying completely independent execution batches via GPU graph algorithms."
    ])

    # Slide 3: CUDA Implementation & Challenges
    add_content_slide(prs, "CUDA Implementation & Research Challenges", [
        "Avoiding O(n²) Scaling: Implementing efficient Hash-based indexing to bypass naive all-pairs transaction comparisons.",
        "Workload Skew: Managing GPU thread balance when popular accounts (like DEXs) create disproportionately dense conflicts.",
        "Custom CUDA Kernels: Dedicated kernels for transaction analysis, conflict indexing, and parallel prefix scans.",
        "Optimized Data Structures: Utilizing Compressed Sparse Row (CSR) graphs and Bitmaps for high-speed irregular memory access."
    ])

    # Slide 4: Expected Contributions & Evaluation
    add_content_slide(prs, "Expected Contributions & Evaluation", [
        "Core Deliverables: A complete CUDA-accelerated conflict detector, dependency graph constructor, and scheduler pipeline.",
        "Comprehensive Benchmarks: Direct empirical comparison between CPU (Sequential/OpenMP) and GPU (CUDA) performance.",
        "Evaluation Metrics: Preprocessing time, relative speedup ratio, GPU memory bandwidth, and scheduler scalability.",
        "Broader Impact: Actionable data to guide future high-throughput blockchain platforms in adopting GPU-assisted execution."
    ])
    
    # Slide 5: Thank You
    create_thank_you_slide(prs)

    prs.save('Project_Presentation_V2.pptx')
    print("Presentation saved as Project_Presentation_V2.pptx")

if __name__ == '__main__':
    main()
