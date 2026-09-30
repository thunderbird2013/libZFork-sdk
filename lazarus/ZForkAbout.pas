{******************************************************************************
  Copyright (C) 2026 Matthias Stoltze
  https://solid-software.net

  ZForkAbout — About-Dialog fuer libZFork (Lazarus)

  Was dieses Unit kann:
  - Modalen Dialog erzeugen (UI im Code, kein separates Form-Resource)
  - Logo, Produktname, Copyright, klickbare URL
  - Lizenz-Memo mit ZForkGetAboutLicenseText
  - Aufruf: ShowZForkAbout / ShowZForkAboutDialog
******************************************************************************}
unit ZForkAbout;

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, ExtCtrls;

type
  TfrmZForkAbout = class(TForm)
  private
    FPnlTop: TPanel;
    FImgLogo: TImage;
    FLblTitle: TLabel;
    FLblCopyright: TLabel;
    FLblUrl: TLabel;
    FMemLicenses: TMemo;
    FBtnOk: TButton;
    procedure UrlClick(Sender: TObject);
    procedure BuildUi;
  public
    constructor Create(AOwner: TComponent); override;
    class procedure Execute;
  end;

procedure ShowZForkAbout;

implementation

uses
  LCLIntf, LResources, ZForkLicenses;

procedure ShowZForkAbout;
begin
  TfrmZForkAbout.Execute;
end;

class procedure TfrmZForkAbout.Execute;
begin
  with TfrmZForkAbout.Create(nil) do
  try
    ShowModal;
  finally
    Free;
  end;
end;

constructor TfrmZForkAbout.Create(AOwner: TComponent);
begin
  inherited CreateNew(AOwner);
  BuildUi;
end;

procedure TfrmZForkAbout.BuildUi;
var
  PnlText: TPanel;
begin
  BorderStyle := bsDialog;
  Position := poScreenCenter;
  Width := 580;
  Height := 560;
  Caption := 'About ' + ZFORK_PRODUCT_NAME;
  Font.Name := 'Segoe UI';
  Font.Size := 9;
  Color := clBtnFace;

  FPnlTop := TPanel.Create(Self);
  FPnlTop.Parent := Self;
  FPnlTop.Align := alTop;
  FPnlTop.Height := 200;
  FPnlTop.BevelOuter := bvNone;
  FPnlTop.Color := clBlack;
  FPnlTop.ParentBackground := False;
  FPnlTop.ParentColor := False;
  FPnlTop.BorderSpacing.Around := 0;

  { Logo: volle Breite links → rechts, leicht nach unten für Proportion }
  FImgLogo := TImage.Create(Self);
  FImgLogo.Parent := FPnlTop;
  FImgLogo.Align := alTop;
  FImgLogo.Height := 100;
  FImgLogo.BorderSpacing.Left := 0;
  FImgLogo.BorderSpacing.Right := 0;
  FImgLogo.BorderSpacing.Top := 12;
  FImgLogo.BorderSpacing.Bottom := 4;
  FImgLogo.Center := False;
  FImgLogo.Proportional := False;
  FImgLogo.Stretch := True;
  FImgLogo.Transparent := False;
  if LazarusResources.Find('zfork_about_logo') <> nil then
    FImgLogo.Picture.LoadFromLazarusResource('zfork_about_logo');

  PnlText := TPanel.Create(Self);
  PnlText.Parent := FPnlTop;
  PnlText.Align := alClient;
  PnlText.BevelOuter := bvNone;
  PnlText.Color := clBlack;
  PnlText.ParentBackground := False;
  PnlText.ParentColor := False;
  PnlText.BorderSpacing.Left := 14;
  PnlText.BorderSpacing.Right := 14;
  PnlText.BorderSpacing.Top := 6;
  PnlText.BorderSpacing.Bottom := 6;

  FLblTitle := TLabel.Create(Self);
  FLblTitle.Parent := PnlText;
  FLblTitle.Align := alTop;
  FLblTitle.AutoSize := True;
  FLblTitle.Caption := ZFORK_PRODUCT_NAME + ' ' + ZFORK_PRODUCT_VERSION +
    ' for Lazarus / Free Pascal';
  FLblTitle.Font.Style := [fsBold];
  FLblTitle.Font.Size := 10;
  FLblTitle.Font.Color := clWhite;
  FLblTitle.ParentFont := False;
  FLblTitle.Transparent := True;

  FLblCopyright := TLabel.Create(Self);
  FLblCopyright.Parent := PnlText;
  FLblCopyright.Align := alTop;
  FLblCopyright.AutoSize := True;
  FLblCopyright.BorderSpacing.Top := 4;
  FLblCopyright.Caption :=
    'Copyright (C) ' + ZFORK_COPYRIGHT_YEAR + ' ' + ZFORK_COPYRIGHT_HOLDER +
    ' — ' + ZFORK_COPYRIGHT_ORG;
  FLblCopyright.Font.Color := $AAAAAA;
  FLblCopyright.ParentFont := False;
  FLblCopyright.Transparent := True;

  FLblUrl := TLabel.Create(Self);
  FLblUrl.Parent := PnlText;
  FLblUrl.Align := alTop;
  FLblUrl.AutoSize := True;
  FLblUrl.BorderSpacing.Top := 2;
  FLblUrl.Caption := ZFORK_PRODUCT_URL;
  FLblUrl.Font.Color := $FFAA40;
  FLblUrl.Font.Style := [fsUnderline];
  FLblUrl.ParentFont := False;
  FLblUrl.Transparent := True;
  FLblUrl.Cursor := crHandPoint;
  FLblUrl.OnClick := @UrlClick;

  FBtnOk := TButton.Create(Self);
  FBtnOk.Parent := Self;
  FBtnOk.Caption := 'OK';
  FBtnOk.Default := True;
  FBtnOk.ModalResult := mrOk;
  FBtnOk.SetBounds(Width - 120, Height - 52, 96, 30);
  FBtnOk.Anchors := [akRight, akBottom];

  FMemLicenses := TMemo.Create(Self);
  FMemLicenses.Parent := Self;
  FMemLicenses.SetBounds(12, 210, Width - 24, Height - 274);
  FMemLicenses.Anchors := [akTop, akLeft, akRight, akBottom];
  FMemLicenses.ReadOnly := True;
  FMemLicenses.ScrollBars := ssBoth;
  FMemLicenses.WordWrap := False;
  FMemLicenses.Font.Name := 'Consolas';
  FMemLicenses.Font.Size := 8;
  FMemLicenses.Lines.Text := ZForkGetAboutLicenseText;
end;

procedure TfrmZForkAbout.UrlClick(Sender: TObject);
begin
  OpenURL(ZFORK_PRODUCT_URL);
end;

initialization
  {$I zfork_about_logo.lrs}

end.
