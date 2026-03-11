import os
import sys
from PIL import Image, ImageOps, ImageDraw, ImageChops

# =============================================================================
# CONFIGURATION
# =============================================================================

# Name of your source logo file in the current directory
SOURCE_LOGO = "image.png"

# Path to the source code 'src' directory relative to this script
# Adjust this if your folder structure is different
SRC_DIR = "src"

# Paths to the target resource folders
ICONS_DIR = os.path.join(SRC_DIR, "qt/res/icons")
IMAGES_DIR = os.path.join(SRC_DIR, "qt/res/icons")

# =============================================================================
# PROCESSING LOGIC
# =============================================================================

def remove_white_bg(img):
    """
    Makes the white background transparent.
    """
    img = img.convert("RGBA")
    datas = img.getdata()

    newData = []
    for item in datas:
        # Change all white (also shades of whites)
        # to transparent
        if item[0] > 220 and item[1] > 220 and item[2] > 220:
            newData.append((255, 255, 255, 0))
        else:
            newData.append(item)

    img.putdata(newData)
    return img

def crop_transparent(img):
    """
    Crops the image to the non-transparent content.
    """
    box = img.getbbox()
    if box:
        return img.crop(box)
    return img

def make_square(img):
    """
    Pads the image with transparency to make it square.
    """
    size = max(img.size)
    new_img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    new_img.paste(img, ((size - img.width) // 2, (size - img.height) // 2))
    return new_img

def make_circular(img, padding=6):
    """
    Crops the image into a circle with a transparent background.
    padding: number of pixels to shave off the edge to remove artifacts.
    """
    # Create a mask
    mask = Image.new('L', img.size, 0)
    draw = ImageDraw.Draw(mask)
    # Draw ellipse slightly smaller than the image to cut off edges
    draw.ellipse((padding, padding, img.size[0] - padding, img.size[1] - padding), fill=255)
    
    # Fit the image into the mask
    output = ImageOps.fit(img, mask.size, centering=(0.5, 0.5))
    output.putalpha(mask)
    return output

def make_canvas(img, target_width, target_height):
    """
    Pads the image into a centered canvas of the specific target size.
    """
    canvas = Image.new('RGBA', (target_width, target_height), (0, 0, 0, 0))
    # content should be centered
    # resize img to fit within target_width x target_height maintaining aspect
    ratio = min(target_width / img.width, target_height / img.height)
    new_w = int(img.width * ratio)
    new_h = int(img.height * ratio)
    img_resized = img.resize((new_w, new_h), Image.LANCZOS)
    
    canvas.paste(img_resized, ((target_width - new_w) // 2, (target_height - new_h) // 2))
    return canvas

def update_branding():
    # 1. Validate paths
    if not os.path.exists(SOURCE_LOGO):
        print(f"Error: Source image '{SOURCE_LOGO}' not found in current directory.")
        return

    if not os.path.exists(ICONS_DIR) or not os.path.exists(IMAGES_DIR):
        print(f"Error: Target directories not found.")
        print(f"Checked: {ICONS_DIR}")
        print(f"Checked: {IMAGES_DIR}")
        print("Are you running this script from the project root?")
        return

    print(f"Processing '{SOURCE_LOGO}'...")
    
    try:
        # Open the source image
        img = Image.open(SOURCE_LOGO).convert("RGBA")
        
        # Remove white background and crop
        img = remove_white_bg(img)
        img = crop_transparent(img)
        
        # Make the image circular and transparent (keeping it tight/square for now)
        # Note: We first square it to ensure circle is a circle, then we can place in rectangular canvas
        img = make_square(img)
        img = make_circular(img)
        
        # ---------------------------------------------------------
        # BCHN Aspect Ratio Logic
        # The original bitcoin.png and splash.png are 1024x1273.
        # We must replicate this canvas size to prevent distortion.
        # ---------------------------------------------------------
        
        ORIG_W = 1024
        ORIG_H = 1273
        
        # 1. Create Main Icon (bitcoin.png) - Rectangular
        # ---------------------------------------------------------
        # We put our square circle into the 1024x1273 canvas
        icon_rect = make_canvas(img, ORIG_W, ORIG_H)
        target_png = os.path.join(ICONS_DIR, "bitcoin.png")
        icon_rect.save(target_png)
        print(f"[OK] Generated: {target_png} ({ORIG_W}x{ORIG_H})")

        # ---------------------------------------------------------
        # 2. Create Windows Icon (bitcoin.ico)
        # ---------------------------------------------------------
        # ICOs usually strictly square. We'll use the tight square 'img' for this
        # or should we use the rect? Windows icons are usually square. 
        # Let's stick to square for ICO to be safe for OS display.
        ico_sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)]
        target_ico = os.path.join(ICONS_DIR, "bitcoin.ico")
        img.save(target_ico, format='ICO', sizes=ico_sizes)
        print(f"[OK] Generated: {target_ico} (Square)")

        # ---------------------------------------------------------
        # 3. Create Testnet Icon (bitcoin_testnet.png)
        # ---------------------------------------------------------
        # Must match bitcoin.png aspect ratio (Rectangular)
        if img.mode != 'RGBA':
            img = img.convert('RGBA')
        
        # Create grayscale version of the SQUARE image first
        r, g, b, a = img.split()
        gray = img.convert("L")
        gray_img_square = Image.merge("RGBA", (gray, gray, gray, a))
        
        # Now put into rectangular canvas
        testnet_icon_rect = make_canvas(gray_img_square, ORIG_W, ORIG_H)
        
        target_testnet = os.path.join(ICONS_DIR, "bitcoin_testnet.png")
        testnet_icon_rect.save(target_testnet)
        print(f"[OK] Generated: {target_testnet} ({ORIG_W}x{ORIG_H})")

        # ---------------------------------------------------------
        # 4. Create Splash Screen (bitcoin_splash.png)
        # ---------------------------------------------------------
        # Same rectangular dimensions
        target_splash = os.path.join(IMAGES_DIR, "bitcoin_splash.png")
        # Reuse icon_rect since it's the high-res 1024x1273
        icon_rect.save(target_splash)
        print(f"[OK] Generated: {target_splash} ({ORIG_W}x{ORIG_H})")

        # ---------------------------------------------------------
        # 5. Create About Dialog Icon (about.png)
        # ---------------------------------------------------------
        # Original was 128x128 (Square). Keep it square.
        target_about = os.path.join(ICONS_DIR, "about.png")
        about_icon = img.resize((128, 128), Image.LANCZOS)
        about_icon.save(target_about)
        print(f"[OK] Generated: {target_about} (128x128)")

        # ---------------------------------------------------------
        # 6. Create No-Letters Icons
        # ---------------------------------------------------------
        # Assume these should match the main bitcoin.png structure (Rectangular)
        target_noletters = os.path.join(ICONS_DIR, "bitcoin_noletters.png")
        icon_rect.save(target_noletters)
        print(f"[OK] Generated: {target_noletters}")
        
        target_noletters_testnet = os.path.join(ICONS_DIR, "bitcoin_noletters_testnet.png")
        testnet_icon_rect.save(target_noletters_testnet)
        print(f"[OK] Generated: {target_noletters_testnet}")
        
        # ---------------------------------------------------------
        # 7. Create Testnet ICO (bitcoin_testnet.ico)
        # ---------------------------------------------------------
        # ICO square
        target_testnet_ico = os.path.join(ICONS_DIR, "bitcoin_testnet.ico")
        testnet_square_icon = gray_img_square
        testnet_square_icon.save(target_testnet_ico, format='ICO', sizes=ico_sizes)
        print(f"[OK] Generated: {target_testnet_ico} (Square)")

        print("\nSUCCESS! All branding files have been updated.")
        print("Run 'ninja -C build' to apply changes.")

    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    update_branding()
