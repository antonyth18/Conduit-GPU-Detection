import os
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.enum.text import PP_ALIGN
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE

def create_title_slide(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    
    # Very faint blue/grey gradient or solid background as seen in photo
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(228, 235, 241)

    # Date Top Right
    txBox_date = slide.shapes.add_textbox(Inches(7.5), Inches(0.2), Inches(2.3), Inches(0.5))
    tf_d = txBox_date.text_frame
    p_d = tf_d.paragraphs[0]
    p_d.text = "18th Aug 2026"
    p_d.font.name = "Arial"
    p_d.font.size = Pt(16)
    p_d.font.color.rgb = RGBColor(0, 0, 0)
    p_d.alignment = PP_ALIGN.RIGHT

    # Main Project Title
    txBox_title = slide.shapes.add_textbox(Inches(0.5), Inches(0.7), Inches(9.0), Inches(1.5))
    tf_t = txBox_title.text_frame
    tf_t.word_wrap = True
    p_t = tf_t.paragraphs[0]
    p_t.text = "GPU-Accelerated Conflict Detection and Dependency Graph Construction for Parallel Smart Contract Execution using CUDA"
    p_t.font.name = "Arial"
    p_t.font.bold = True
    p_t.font.underline = True
    p_t.font.size = Pt(28)
    p_t.font.color.rgb = RGBColor(0, 0, 0)
    p_t.alignment = PP_ALIGN.CENTER

    # Logo Center
    # Check if logo exists
    if os.path.exists('nitk_logo.png'):
        # Logo in the center. Picture height ~1.5 inches
        slide.shapes.add_picture('nitk_logo.png', Inches(4.25), Inches(2.2), width=Inches(1.5))

    # IT 301 - Parallel Computing
    txBox_course = slide.shapes.add_textbox(Inches(1.0), Inches(4.0), Inches(8.0), Inches(0.5))
    tf_c = txBox_course.text_frame
    p_c = tf_c.paragraphs[0]
    p_c.text = "IT 301 - Parallel Computing"
    p_c.font.name = "Arial"
    p_c.font.bold = True
    p_c.font.underline = True
    p_c.font.size = Pt(20)
    p_c.font.color.rgb = RGBColor(0, 0, 0)
    p_c.alignment = PP_ALIGN.CENTER

    # College Name
    txBox_college = slide.shapes.add_textbox(Inches(1.0), Inches(4.4), Inches(8.0), Inches(0.6))
    tf_col = txBox_college.text_frame
    tf_col.word_wrap = True
    p_col = tf_col.paragraphs[0]
    p_col.text = "National Institute of Technology Karnataka Surathkal"
    p_col.font.name = "Arial"
    p_col.font.bold = True
    p_col.font.underline = True
    p_col.font.size = Pt(18)
    p_col.font.color.rgb = RGBColor(0, 0, 0)
    p_col.alignment = PP_ALIGN.CENTER

    # Team Members at bottom center
    txBox_team = slide.shapes.add_textbox(Inches(0.5), Inches(5.0), Inches(9.0), Inches(0.6))
    tf_team = txBox_team.text_frame
    p_team = tf_team.paragraphs[0]
    p_team.text = "Aditya Raj (241IT006) | Antony Thaikadavil (241IT010) | Nikhil Soni (241IT048) | Sucheth K Katte (241IT078)"
    p_team.font.name = "Arial"
    p_team.font.bold = False
    p_team.font.size = Pt(12)
    p_team.font.color.rgb = RGBColor(50, 50, 50)
    p_team.alignment = PP_ALIGN.CENTER

def add_content_slide(prs, title, bullets):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    
    # Pure white background
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(255, 255, 255)

    # Header Bar
    header = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), Inches(10), Inches(1.0))
    header.fill.solid()
    header.fill.fore_color.rgb = RGBColor(15, 23, 42) # Deep Slate Navy
    header.line.fill.background()

    # Accent Line
    line = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(1.0), Inches(10), Inches(0.04))
    line.fill.solid()
    line.fill.fore_color.rgb = RGBColor(14, 165, 233) # Professional Blue Accent
    line.line.fill.background()

    # Title Box (Inside Header)
    txBox = slide.shapes.add_textbox(Inches(0.5), Inches(0.15), Inches(9), Inches(0.8))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = title
    p.font.name = "Arial"
    p.font.bold = True
    p.font.size = Pt(28)
    p.font.color.rgb = RGBColor(255, 255, 255)

    # Content Box
    txBox_content = slide.shapes.add_textbox(Inches(0.5), Inches(1.2), Inches(9.0), Inches(4.3))
    tf_c = txBox_content.text_frame
    tf_c.word_wrap = True
    
    for i, bullet in enumerate(bullets):
        if i == 0:
            p_c = tf_c.paragraphs[0]
        else:
            p_c = tf_c.add_paragraph()
            
        p_c.text = "•  " + bullet
        p_c.font.name = "Arial"
        p_c.font.size = Pt(18)
        p_c.font.color.rgb = RGBColor(31, 41, 55) # Very readable dark text
        p_c.space_after = Pt(14)

def create_thank_you_slide(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    
    # Deep Slate Navy Background
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(15, 23, 42) 

    # Accent Line
    line = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(3.5), Inches(10), Inches(0.08))
    line.fill.solid()
    line.fill.fore_color.rgb = RGBColor(14, 165, 233) 
    line.line.fill.background()

    txBox = slide.shapes.add_textbox(Inches(0.5), Inches(1.5), Inches(9), Inches(2))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = "Thank You"
    p.font.name = "Arial"
    p.font.bold = True
    p.font.size = Pt(54)
    p.font.color.rgb = RGBColor(255, 255, 255)
    p.alignment = PP_ALIGN.CENTER
    
    p2 = tf.add_paragraph()
    p2.text = "Open for Questions & Discussion"
    p2.font.name = "Arial"
    p2.font.size = Pt(24)
    p2.font.color.rgb = RGBColor(186, 230, 253)
    p2.alignment = PP_ALIGN.CENTER
    p2.space_before = Pt(15)

def main():
    prs = Presentation()
    prs.slide_width = Inches(10)
    prs.slide_height = Inches(5.625)

    # Page 1: Title Slide
    create_title_slide(prs)

    # Page 2: Intro to Chosen Problem
    add_content_slide(prs, "Introduction to Chosen Problem", [
        "Modern blockchains (e.g., Ethereum) execute transactions sequentially to guarantee deterministic execution across all validator nodes.",
        "This sequential model severely limits overall system throughput, even when transactions are entirely independent.",
        "Before parallel execution is possible, identifying transaction conflicts and constructing dependency graphs is absolutely required.",
        "For large blocks containing 10,000–100,000 transactions, this preprocessing stage becomes a major computational bottleneck."
    ])

    # Page 3: Background and Motivation
    add_content_slide(prs, "Background and Motivation", [
        "Transactions access specific blockchain states (read/write sets) like user balances or smart contract data.",
        "While some transactions conflict, many others (like independent DEX swaps or transfers) can execute completely simultaneously.",
        "Current parallel execution frameworks (e.g., Block-STM) rely on CPU-based scheduling, which scales poorly as block sizes increase.",
        "There is a massive opportunity to leverage GPU computation (CUDA) to offload and drastically accelerate this preprocessing bottleneck."
    ])

    # Page 4: Uniqueness/Novelty
    add_content_slide(prs, "Uniqueness/Novelty", [
        "Applying massively parallel GPU architecture specifically to the conflict detection and dependency construction phases, rather than just execution.",
        "Utilizing highly efficient GPU Hash Indexing to completely bypass slow, naive O(n²) all-pairs transaction comparisons.",
        "Implementing optimized data structures like Compressed Sparse Row (CSR) and Bitmaps for rapid, irregular GPU memory access.",
        "Addressing workload skew dynamically by balancing CUDA thread blocks when popular smart contracts generate dense conflicts."
    ])

    # Page 5: Evaluation
    add_content_slide(prs, "Evaluation", [
        "The project implements a comprehensive empirical benchmarking methodology across simulated blocks of 10k to 100k transactions.",
        "Direct performance comparison between Sequential CPU, Multi-threaded CPU (OpenMP), and CUDA-accelerated GPU implementations.",
        "Key performance metrics include conflict detection time, dependency graph construction speed, and total memory utilization.",
        "Evaluating system performance under varying workload characteristics, particularly high-conflict versus low-conflict transaction densities."
    ])

    # Page 6: Expected Outcomes
    add_content_slide(prs, "Expected Outcomes", [
        "A complete, functional CUDA-accelerated preprocessing engine for blockchain transaction conflict detection and scheduling.",
        "Empirical quantification of the precise speedup ratio achieved by GPU offloading compared to traditional CPU baselines.",
        "Scalability analysis demonstrating how the GPU architecture handles massive block sizes and skewed account access patterns.",
        "Actionable guidance and benchmarking data for integrating GPU-assisted execution into future high-throughput blockchain platforms."
    ])

    # Page 7: Thank You Slide
    create_thank_you_slide(prs)

    prs.save('Final_Project_Presentation_V3.pptx')
    print("Presentation saved as Final_Project_Presentation_V3.pptx")

if __name__ == '__main__':
    main()
